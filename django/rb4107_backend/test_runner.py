"""Test runner that pins what the tests are written against, whatever django/.env says."""
from django.conf import settings
from django.test.runner import DiscoverRunner


class Runner(DiscoverRunner):
    def setup_test_environment(self, **kwargs):
        super().setup_test_environment(**kwargs)
        # The tests use the single-controller tree (rb4107/controller/state);
        # tests of per-controller trees override this themselves.
        settings.RB4107_MQTT = {**settings.RB4107_MQTT, "TOPIC": "rb4107/#"}
