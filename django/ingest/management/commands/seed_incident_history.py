"""Fill the simulated stalls with made-up past incidents for the Stall history view, and the last 24 hours
of made-up controller events for their Event log (demo only)."""
from django.core.management.base import BaseCommand
from django.utils import timezone

from ingest.demo_events import DEMO_EVENT_TOPIC, clear_demo_events, seed_demo_events
from ingest.incidents import DEMO_PROFILES, clear_demo, seed_demo
from ingest.locations import load_location_catalog
from ingest.models import Device, Incident, SafetyEvent


class Command(BaseCommand):
    help = ("Add made-up past incidents (marked demo), cooking days and 24 hours of events to the catalogue's "
            "demo_only stalls.")

    def add_arguments(self, parser):
        parser.add_argument("--days", type=int, default=90, help="How far back the history goes")
        parser.add_argument("--clear", action="store_true", help="Delete every demo incident and event, and stop")

    def handle(self, *args, **options):
        if options["clear"]:
            self.stdout.write(f"Deleted {clear_demo()} demo incidents and their cooking days, "
                              f"and {clear_demo_events()} demo events")
            return
        catalog = load_location_catalog()["devices"]
        ids = [key for key, value in catalog.items() if value.get("demo_only")]
        now, total, events = timezone.now(), 0, 0
        profiles = list(DEMO_PROFILES)  # each stall gets its own habits, in turn
        for index, device_id in enumerate(sorted(ids)):
            device, _ = Device.objects.get_or_create(device_id=device_id)
            if not Incident.objects.filter(device=device, demo=True).exists():  # once per database
                total += seed_demo(device, now, days=options["days"], profile=profiles[index % len(profiles)])
            if not SafetyEvent.objects.filter(device=device, message__topic=DEMO_EVENT_TOPIC).exists():
                events += seed_demo_events(device, catalog[device_id].get("demo_scenario"), now)
        self.stdout.write(f"Added {total} demo incidents and {events} demo events to {len(ids)} simulated stalls")
