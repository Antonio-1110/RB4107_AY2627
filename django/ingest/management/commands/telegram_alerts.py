"""python manage.py telegram_alerts [--find-chats | --test]

Set up the Telegram alerts (docs/telegram_alerts.md). With no option it shows
the current settings. --find-chats lists the chats that have written to the bot
recently, with the ID to put in RB4107_TELEGRAM_CHAT_IDS. --test sends a test
message to every configured chat.
"""
from django.conf import settings
from django.core.management.base import BaseCommand, CommandError

from ingest import alerts


class Command(BaseCommand):
    help = "Find chat IDs for, or send a test message to, the Telegram alert chats."

    def add_arguments(self, parser):
        group = parser.add_mutually_exclusive_group()
        group.add_argument("--find-chats", action="store_true", help="list chats that messaged the bot")
        group.add_argument("--test", action="store_true", help="send a test alert to every configured chat")

    def handle(self, *args, **options):
        if not settings.RB4107_TELEGRAM_BOT_TOKEN:
            raise CommandError("RB4107_TELEGRAM_BOT_TOKEN is not set (put it in django/.env).")
        if options["find_chats"]:
            self.find_chats()
        elif options["test"]:
            self.test()
        else:
            self.show()

    def show(self):
        me = self.call("getMe")["result"]
        self.stdout.write(f"bot: @{me['username']}")
        self.stdout.write(f"chats: {', '.join(settings.RB4107_TELEGRAM_CHAT_IDS) or 'none (alerts are off)'}")
        self.stdout.write(f"events: {', '.join(settings.RB4107_ALERT_EVENTS) or 'none'}")
        unknown = set(settings.RB4107_ALERT_EVENTS) - set(alerts.KINDS)
        if unknown:
            self.stdout.write(f"unknown events ignored: {', '.join(sorted(unknown))} "
                              f"(choose from {', '.join(alerts.KINDS)})")

    def find_chats(self):
        chats = {}
        for update in self.call("getUpdates")["result"]:
            for key in ("message", "channel_post", "my_chat_member"):
                chat = update.get(key, {}).get("chat")
                if chat:
                    chats[chat["id"]] = chat.get("title") or chat.get("username") or chat.get("first_name", "")
        if not chats:
            self.stdout.write("No chats yet. Send the bot a message (or add it to the group and "
                              "write something there), then run this again.")
        for chat_id, name in chats.items():
            self.stdout.write(f"{chat_id}\t{name}")

    def test(self):
        if not settings.RB4107_TELEGRAM_CHAT_IDS:
            raise CommandError("RB4107_TELEGRAM_CHAT_IDS is empty; run --find-chats first.")
        sent = alerts.send("✅ RB4107 test alert: Telegram alerts are working.")
        self.stdout.write(f"sent to {sent} of {len(settings.RB4107_TELEGRAM_CHAT_IDS)} chats")
        if sent < len(settings.RB4107_TELEGRAM_CHAT_IDS):
            raise CommandError("Some chats failed; see the log line above.")

    def call(self, method):
        try:
            return alerts.call(method)
        except (OSError, RuntimeError, ValueError) as error:
            raise CommandError(f"Telegram {method} failed: {error}") from None
