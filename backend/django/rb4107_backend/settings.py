"""
Django settings for the RB4107 backend.

Scope (TODO sections 23-24): MQTT ingestion only. There is no frontend or
dashboard yet. Every deployment-specific value comes from an environment
variable, so nothing about the MacBook or broker is hard-coded.
"""

import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = BASE_DIR.parent.parent


def env(name: str, default: str) -> str:
    return os.environ.get(name, default)


# Development defaults. Set DJANGO_SECRET_KEY / DJANGO_DEBUG for anything
# beyond the bench.
SECRET_KEY = env("DJANGO_SECRET_KEY", "dev-only-insecure-key-change-me")
DEBUG = env("DJANGO_DEBUG", "1") == "1"
ALLOWED_HOSTS = [h for h in env("DJANGO_ALLOWED_HOSTS", "localhost,127.0.0.1").split(",") if h]

INSTALLED_APPS = [
    "django.contrib.contenttypes",
    "django.contrib.auth",
    "ingest",
]

MIDDLEWARE = [
    "django.middleware.security.SecurityMiddleware",
    "django.middleware.common.CommonMiddleware",
]

ROOT_URLCONF = "rb4107_backend.urls"
WSGI_APPLICATION = "rb4107_backend.wsgi.application"
ASGI_APPLICATION = "rb4107_backend.asgi.application"

DATABASES = {
    "default": {
        "ENGINE": "django.db.backends.sqlite3",
        "NAME": BASE_DIR / "db.sqlite3",
    }
}

LANGUAGE_CODE = "en-us"
TIME_ZONE = env("DJANGO_TIME_ZONE", "Asia/Singapore")
USE_I18N = False
USE_TZ = True
DEFAULT_AUTO_FIELD = "django.db.models.BigAutoField"

# --- RB4107 MQTT ingestion -------------------------------------------------

RB4107_MQTT = {
    # Mosquitto on the MacBook. The subscriber usually runs on the same
    # machine, hence localhost.
    "HOST": env("RB4107_MQTT_HOST", "localhost"),
    "PORT": int(env("RB4107_MQTT_PORT", "1883")),
    "USERNAME": env("RB4107_MQTT_USERNAME", "") or None,
    "PASSWORD": env("RB4107_MQTT_PASSWORD", "") or None,
    "TOPIC": env("RB4107_MQTT_TOPIC", "rb4107/#"),
    "QOS": int(env("RB4107_MQTT_QOS", "1")),
    # A fixed client ID plus a persistent session means the broker queues
    # QoS 1 events (warnings, shutdowns, faults) while the subscriber is
    # down, and delivers them when it comes back.
    "CLIENT_ID": env("RB4107_MQTT_CLIENT_ID", "rb4107-django-subscriber"),
    "PERSISTENT_SESSION": env("RB4107_MQTT_PERSISTENT_SESSION", "1") == "1",
    "KEEPALIVE_S": int(env("RB4107_MQTT_KEEPALIVE_S", "30")),
    "RECONNECT_MIN_S": int(env("RB4107_MQTT_RECONNECT_MIN_S", "1")),
    "RECONNECT_MAX_S": int(env("RB4107_MQTT_RECONNECT_MAX_S", "30")),
}

# The same JSON Schema the firmware is tested against (docs/schema).
RB4107_SCHEMA_FILE = Path(env("RB4107_SCHEMA_FILE", str(REPO_ROOT / "docs" / "schema" / "rb4107_mqtt.schema.json")))
RB4107_SUPPORTED_SCHEMA_VERSIONS = {2}

# --- Logging ---------------------------------------------------------------
# Log lines look like the firmware's: [MQTT][INFO] broker connected

LOG_LEVEL = env("RB4107_LOG_LEVEL", "INFO")

LOGGING = {
    "version": 1,
    "disable_existing_loggers": False,
    "filters": {
        # Adds %(rb_tag)s: "rb4107.mqtt" -> "MQTT".
        "rb_tag": {"()": "ingest.log.RbTagFilter"},
    },
    "formatters": {
        "rb4107": {"format": "%(asctime)s [%(rb_tag)s][%(levelname)s] %(message)s"},
    },
    "handlers": {
        "console": {"class": "logging.StreamHandler", "formatter": "rb4107", "filters": ["rb_tag"]},
    },
    "loggers": {
        "rb4107": {"handlers": ["console"], "level": LOG_LEVEL, "propagate": False},
    },
    "root": {"handlers": ["console"], "level": "WARNING"},
}
