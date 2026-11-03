"""Fill the simulated stalls with made-up past incidents for the Stall history view (demo only)."""
from django.core.management.base import BaseCommand
from django.utils import timezone

from ingest.incidents import DEMO_PROFILES, clear_demo, seed_demo
from ingest.locations import load_location_catalog
from ingest.models import Device, Incident


class Command(BaseCommand):
    help = "Add made-up past incidents (marked demo) and cooking days to the catalogue's demo_only stalls."

    def add_arguments(self, parser):
        parser.add_argument("--days", type=int, default=90, help="How far back the history goes")
        parser.add_argument("--clear", action="store_true", help="Delete every demo incident and stop")

    def handle(self, *args, **options):
        if options["clear"]:
            self.stdout.write(f"Deleted {clear_demo()} demo incidents and their cooking days")
            return
        ids = [key for key, value in load_location_catalog()["devices"].items() if value.get("demo_only")]
        now, total = timezone.now(), 0
        profiles = list(DEMO_PROFILES)  # each stall gets its own habits, in turn
        for index, device_id in enumerate(sorted(ids)):
            device, _ = Device.objects.get_or_create(device_id=device_id)
            if not Incident.objects.filter(device=device, demo=True).exists():  # once per database
                total += seed_demo(device, now, days=options["days"], profile=profiles[index % len(profiles)])
        self.stdout.write(f"Added {total} demo incidents to {len(ids)} simulated stalls")
