import copy
import dataclasses
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest import mock

from django.test import SimpleTestCase, override_settings

from ingest import alerts, handlers, validation

SAMPLES = [json.loads(line) for line in (Path(__file__).parent / "firmware_samples.jsonl").read_text().splitlines()]
ON = dict(RB4107_TELEGRAM_BOT_TOKEN="123:abc", RB4107_TELEGRAM_CHAT_IDS=["-100", "42"],
          RB4107_ALERT_EVENTS=["warning", "shutdown"], RB4107_ALERT_MAX_AGE_S=600)


def event(name, state=None, **changes):
    sample = copy.deepcopy(next(s for s in SAMPLES if s["payload"].get("event") == name))
    if state:
        sample["payload"]["safety"]["state"] = state
    sample["payload"].update(changes)
    return validation.parse(sample["topic"], json.dumps(sample["payload"]).encode())


class _NoThread:
    """Run the background send inline so the test can see it."""
    def __init__(self, target, args, **_):
        self.target, self.args = target, args

    def start(self):
        self.target(*self.args)


@override_settings(**ON)
@mock.patch("ingest.alerts.threading.Thread", _NoThread)
@mock.patch("ingest.alerts.call")
class TelegramAlertTest(SimpleTestCase):
    def test_buzzer_and_shutdown_go_to_every_chat(self, call):
        for name in ("warning", "shutdown"):
            with self.subTest(event=name):
                call.reset_mock()
                alerts.notify(event(name))
                self.assertEqual([c.args[1]["chat_id"] for c in call.call_args_list], ["-100", "42"])
                self.assertEqual(call.call_args.args[0], "sendMessage")
        self.assertIn("Reset shutdown", call.call_args.args[1]["text"])

    def test_text_names_the_stall(self, call):
        alerts.notify(event("warning"))
        text = call.call_args.args[1]["text"]
        self.assertIn("Buzzer sounding", text)
        self.assertIn("controller_01", text)
        self.assertIn("Unattended for: 5 s", text)

    def test_unattended_and_faults_are_off_by_default(self, call):
        alerts.notify(event("state_change", state="UNATTENDED"))
        alerts.notify(event("fault_raised"))
        call.assert_not_called()

    @override_settings(RB4107_ALERT_EVENTS=["unattended", "fault"])
    def test_unattended_and_fault_can_be_turned_on(self, call):
        alerts.notify(event("state_change", state="UNATTENDED"))
        alerts.notify(event("state_change", state="FAULT"))
        alerts.notify(event("warning"))
        texts = [c.args[1]["text"] for c in call.call_args_list]
        self.assertEqual(len(texts), 4)
        self.assertIn("timer has started", texts[0])
        self.assertIn("FAULT", texts[2])

    @override_settings(RB4107_TELEGRAM_CHAT_IDS=[])
    def test_off_without_a_chat(self, call):
        alerts.notify(event("shutdown"))
        call.assert_not_called()

    def test_old_queued_event_is_skipped(self, call):
        stale = (datetime.now(timezone.utc) - timedelta(hours=1)).isoformat()
        fresh = datetime.now(timezone.utc).isoformat()
        alerts.notify(event("shutdown", timestamp=stale))
        call.assert_not_called()
        alerts.notify(event("shutdown", timestamp=fresh))
        self.assertEqual(call.call_count, 2)

    def test_retained_event_is_not_resent(self, call):
        alerts.notify(dataclasses.replace(event("shutdown"), retained=True))
        call.assert_not_called()

    def test_a_failing_chat_does_not_stop_the_others(self, call):
        call.side_effect = [RuntimeError("chat not found"), {"ok": True}]
        with self.assertLogs("rb4107.alerts", "WARNING") as logs:
            alerts.notify(event("shutdown"))
        self.assertEqual(call.call_count, 2)
        self.assertIn("chat not found", logs.output[0])
        self.assertNotIn("123:abc", logs.output[0])

    def test_handler_sends_the_alert(self, call):
        with mock.patch("ingest.handlers.persist"), self.assertLogs("rb4107.event", "WARNING"):
            handlers.handle(event("warning"))
        self.assertEqual(call.call_count, 2)


class TelegramCallTest(SimpleTestCase):
    @override_settings(**ON)
    def test_telegram_error_is_raised_with_its_description(self):
        reply = mock.MagicMock()
        reply.__enter__.return_value.read.return_value = b'{"ok": false, "description": "Unauthorized"}'
        with mock.patch("ingest.alerts.urllib.request.urlopen", return_value=reply) as urlopen:
            with self.assertRaisesRegex(RuntimeError, "Unauthorized"):
                alerts.call("getMe")
        self.assertEqual(urlopen.call_args.args[0].full_url, "https://api.telegram.org/bot123:abc/getMe")
