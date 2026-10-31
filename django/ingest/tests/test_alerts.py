import copy
import dataclasses
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest import mock

import threading
import urllib.parse

from django.test import SimpleTestCase, TestCase, override_settings

from ingest import alerts, handlers, validation
from ingest.models import TelegramAlert

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
@mock.patch("ingest.alerts.connections", mock.MagicMock())  # the send thread's clean-up; not the test's DB
@mock.patch("ingest.alerts.threading.Thread", _NoThread)
@mock.patch("ingest.alerts.call")
class TelegramAlertTest(TestCase):
    def test_buzzer_and_shutdown_go_to_every_chat(self, call):
        for name in ("warning", "shutdown"):
            with self.subTest(event=name):
                call.reset_mock()
                alerts.notify(event(name))
                self.assertEqual([c.args[1]["chat_id"] for c in call.call_args_list], ["-100", "42"])
                self.assertEqual(call.call_args.args[0], "sendMessage")
        self.assertIn("Reset shutdown", call.call_args.args[1]["text"])

    def test_poll_asks_for_button_presses_only(self, call):
        stop = mock.MagicMock()
        stop.is_set.side_effect = [False, True]
        call.return_value = {"ok": True, "result": []}
        alerts.poll_acks(stop)
        self.assertEqual(call.call_args.args[1]["allowed_updates"], ["callback_query"])
        self.assertGreater(call.call_args.kwargs["timeout"], alerts.POLL_S)

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


def press(alert_id, chat_id=-100, message_id=7, user=None, data=None):
    return {"id": "cb1", "data": data or f"ack:{alert_id}",
            "from": user or {"first_name": "Antonio", "username": "antonio"},
            "message": {"message_id": message_id, "chat": {"id": chat_id}}}


@override_settings(**ON)
@mock.patch("ingest.alerts.connections", mock.MagicMock())
@mock.patch("ingest.alerts.threading.Thread", _NoThread)
@mock.patch("ingest.alerts.call")
class AcknowledgeTest(TestCase):
    def send_shutdown(self, call):
        call.side_effect = lambda method, params=None, **_: {"ok": True, "result": {"message_id": 7 if params["chat_id"] == "-100" else 8}}
        alerts.notify(event("shutdown"))
        call.reset_mock(side_effect=True)
        call.return_value = {"ok": True}
        return TelegramAlert.objects.get()

    def test_alert_has_the_button_and_is_recorded(self, call):
        alert = self.send_shutdown(call)
        self.assertEqual(alert.kind, "shutdown")
        self.assertEqual(alert.controller_id, "controller_01")
        self.assertEqual(alert.messages, [["-100", 7], ["42", 8]])
        self.assertIsNone(alert.acked_at)
        call.side_effect = None
        alerts.notify(event("warning"))
        markup = call.call_args.args[1]["reply_markup"]
        self.assertEqual(markup["inline_keyboard"][0][0]["callback_data"], f"ack:{alert.pk + 1}")

    def test_press_records_who_and_updates_every_chat(self, call):
        alert = self.send_shutdown(call)
        with self.assertLogs("rb4107.alerts", "INFO"):
            alerts.handle_press(press(alert.pk))
        alert.refresh_from_db()
        self.assertEqual(alert.acked_by, "Antonio (@antonio)")
        self.assertIsNotNone(alert.acked_at)
        answer = next(c for c in call.call_args_list if c.args[0] == "answerCallbackQuery")
        self.assertEqual(answer.args[1], {"callback_query_id": "cb1", "text": "Acknowledged. Thank you."})
        edits = sorted((c.args[1]["chat_id"], c.args[1]["message_id"]) for c in call.call_args_list
                       if c.args[0] == "editMessageText")
        self.assertEqual(edits, [("-100", 7), ("42", 8)])
        edit = next(c for c in call.call_args_list if c.args[0] == "editMessageText").args[1]
        self.assertIn("Cooker SHUT DOWN", edit["text"])
        self.assertIn("✅ Acknowledged by Antonio (@antonio) at ", edit["text"])
        self.assertNotIn("reply_markup", edit)  # the button goes away

    def test_only_the_first_press_counts(self, call):
        alert = self.send_shutdown(call)
        with self.assertLogs("rb4107.alerts", "INFO"):
            alerts.handle_press(press(alert.pk))
        call.reset_mock()
        alerts.handle_press(press(alert.pk, chat_id=42, message_id=8, user={"first_name": "Ben"}))
        alert.refresh_from_db()
        self.assertEqual(alert.acked_by, "Antonio (@antonio)")
        self.assertEqual([c.args[0] for c in call.call_args_list], ["answerCallbackQuery"])
        self.assertIn("Already acknowledged by Antonio", call.call_args.args[1]["text"])

    def test_press_from_an_unknown_chat_or_alert_is_refused(self, call):
        alert = self.send_shutdown(call)
        for query in (press(alert.pk, chat_id=999), press(alert.pk + 50), press(0, data="other")):
            alerts.handle_press(query)
        alert.refresh_from_db()
        self.assertIsNone(alert.acked_at)
        self.assertEqual({c.args[0] for c in call.call_args_list}, {"answerCallbackQuery"})
        self.assertIn("not known", call.call_args.args[1]["text"])

    def test_poller_handles_presses_and_confirms_them(self, call):
        alert = self.send_shutdown(call)
        stop = threading.Event()
        polls = []

        def fake(method, params=None, **_):
            if method != "getUpdates":
                return {"ok": True}
            polls.append(params["offset"])
            if len(polls) == 1:
                return {"ok": True, "result": [{"update_id": 500, "callback_query": press(alert.pk)}]}
            stop.set()
            return {"ok": True, "result": []}

        call.side_effect = fake
        with self.assertLogs("rb4107.alerts", "INFO"):
            alerts.poll_acks(stop)
        self.assertEqual(polls, [None, 501])
        alert.refresh_from_db()
        self.assertIsNotNone(alert.acked_at)

    def test_poller_waits_and_retries_after_an_error(self, call):
        stop = mock.MagicMock()
        stop.is_set.side_effect = [False, False, True]
        call.side_effect = [OSError("no internet"), {"ok": True, "result": []}]
        with self.assertLogs("rb4107.alerts", "WARNING") as logs:
            alerts.poll_acks(stop)
        stop.wait.assert_called_once_with(alerts.RETRY_S)
        self.assertEqual(call.call_count, 2)
        self.assertIn("no internet", logs.output[0])


class TelegramCallTest(SimpleTestCase):
    @override_settings(**ON)
    def test_objects_are_sent_as_json(self):
        reply = mock.MagicMock()
        reply.__enter__.return_value.read.return_value = b'{"ok": true}'
        with mock.patch("ingest.alerts.urllib.request.urlopen", return_value=reply) as urlopen:
            alerts.call("sendMessage", {"chat_id": "-100", "text": "hi", "reply_markup": alerts.ack_button(3),
                                        "skipped": None})
        body = urllib.parse.parse_qs(urlopen.call_args.args[0].data.decode())
        self.assertEqual(json.loads(body["reply_markup"][0]), alerts.ack_button(3))
        self.assertNotIn("skipped", body)

    @override_settings(**ON)
    def test_telegram_error_is_raised_with_its_description(self):
        reply = mock.MagicMock()
        reply.__enter__.return_value.read.return_value = b'{"ok": false, "description": "Unauthorized"}'
        with mock.patch("ingest.alerts.urllib.request.urlopen", return_value=reply) as urlopen:
            with self.assertRaisesRegex(RuntimeError, "Unauthorized"):
                alerts.call("getMe")
        self.assertEqual(urlopen.call_args.args[0].full_url, "https://api.telegram.org/bot123:abc/getMe")
