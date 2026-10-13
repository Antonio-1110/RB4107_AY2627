"""python manage.py prune_history [--dry-run]

Delete old messages according to the retention policy (ingest/retention.py,
RB4107_RETENTION_* in the settings). The MQTT subscriber runs the same job once
an hour, so this is for a first clean-up or a dry run.
"""
from django.core.management.base import BaseCommand

from ingest import retention


class Command(BaseCommand):
    help = "Delete old messages according to the retention policy."

    def add_arguments(self, parser):
        parser.add_argument("--dry-run", action="store_true", help="only count what would be deleted")

    def handle(self, *args, **options):
        policy = retention.Policy.from_settings()
        self.stdout.write(f"policy: {policy}")
        result = retention.prune(policy=policy, dry_run=options["dry_run"])
        verb = "would delete" if options["dry_run"] else "deleted"
        for rule, count in result.items():
            self.stdout.write(f"{verb} {count:>9} ({rule})")
        self.stdout.write("The database file does not shrink by itself; run `sqlite3 db.sqlite3 VACUUM` to give the space back.")
