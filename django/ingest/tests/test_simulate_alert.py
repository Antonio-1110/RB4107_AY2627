from io import StringIO
from unittest import mock

from django.core.management import CommandError, call_command
from django.test import TestCase, override_settings

from ingest.models import Incident, SafetyEvent


@override_settings(RB4107_TELEGRAM_BOT_TOKEN="", RB4107_TELEGRAM_CHAT_IDS=[])
class SimulateAlertTest(TestCase):
    def run_command(self, *args):
        out = StringIO()
        with self.assertLogs("rb4107", "WARNING"):
            call_command("simulate_alert", "--direct", "--attended", "0.05", "--warning-after", "0.1",
                         "--shutdown-after", "0.15", "--reset-after", "0.05", "--interval", "0.01", *args,
                         stdout=out)
        return out.getvalue()

    def test_plays_buzzer_shutdown_and_reset(self):
        with mock.patch("ingest.alerts.notify") as notify:
            out = self.run_command()
        self.assertEqual([e.event_type for e in SafetyEvent.objects.order_by("id")], ["warning", "shutdown"])
        self.assertEqual([c.args[0].data["event"] for c in notify.call_args_list], ["warning", "shutdown"])
        incident = Incident.objects.get()
        self.assertEqual((incident.peak, incident.end_reason), ("shutdown", "operator_reset"))
        for state in ("MONITORING", "UNATTENDED", "WARNING", "SHUTDOWN", "IDLE"):
            self.assertIn(state, out)

    def test_unknown_controller_is_refused(self):
        with self.assertRaisesRegex(CommandError, "not in the location catalogue"):
            call_command("simulate_alert", "--direct", "--controller", "nope", stdout=StringIO())

    def test_shutdown_must_come_after_the_buzzer(self):
        with self.assertRaisesRegex(CommandError, "warning-after"):
            call_command("simulate_alert", "--direct", "--warning-after", "50", "--shutdown-after", "40",
                         stdout=StringIO())
