"""Fill the simulated stalls with made-up past incidents for the Stall history view (demo only)."""
from django.core.management.base import BaseCommand
from django.utils import timezone

from ingest.incidents import seed_demo
from ingest.locations import load_location_catalog
from ingest.models import Device, Incident


class Command(BaseCommand):
    help = "Add made-up past incidents (marked demo) to the catalogue's demo_only stalls."

    def add_arguments(self, parser):
        parser.add_argument("--days", type=int, default=60, help="How far back the history goes")
        parser.add_argument("--clear", action="store_true", help="Delete every demo incident and stop")

    def handle(self, *args, **options):
        if options["clear"]:
            deleted, _ = Incident.objects.filter(demo=True).delete()
            self.stdout.write(f"Deleted {deleted} demo incidents")
            return
        ids = [key for key, value in load_location_catalog()["devices"].items() if value.get("demo_only")]
        now, total = timezone.now(), 0
        for device_id in ids:
            device, _ = Device.objects.get_or_create(device_id=device_id)
            if not Incident.objects.filter(device=device, demo=True).exists():  # once per database
                total += seed_demo(device, now, days=options["days"])
        self.stdout.write(f"Added {total} demo incidents to {len(ids)} simulated stalls")
