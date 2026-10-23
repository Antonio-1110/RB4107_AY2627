"""Telemetry API, C4002 tuning, reset, presence filter and thermal threshold commands, and the dashboard from the repository's frontend/ folder."""
from django.conf import settings
from django.urls import path, re_path
from django.views.decorators.cache import cache_control
from django.views.static import serve as _serve

from ingest import views

# The dashboard files change with every update. Without a Cache-Control header a
# browser decides for itself how long to keep each one, so it can mix an old
# dom.js with a new chart.js and the page never starts. no-cache means "ask
# first": an unchanged file still answers 304.
serve = cache_control(no_cache=True)(_serve)

urlpatterns = [
    path("api/health/", views.health),
    path("api/devices/", views.devices),
    path("api/stall-history/", views.stall_history),
    path("api/devices/<str:device_id>/latest/", views.latest),
    path("api/devices/<str:device_id>/history/", views.history),
    path("api/devices/<str:device_id>/events/", views.events),
    path("api/devices/<str:device_id>/reset/", views.reset),
    path("api/devices/<str:device_id>/presence_filter/", views.presence_filter),
    path("api/devices/<str:device_id>/nodes/<str:node>/c4002/", views.c4002_command),
    path("api/devices/<str:device_id>/nodes/<str:node>/c4002/live/", views.c4002_live),
    path("api/devices/<str:device_id>/thermal_frame/", views.thermal_frame),
    path("api/devices/<str:device_id>/cooking_temp/", views.cooking_temp),
    path("api/devices/<str:device_id>/cookware/", views.cookware),
    path("api/devices/<str:device_id>/tiers/", views.tiers),
    path("api/devices/<str:device_id>/attendance/", views.attendance),
    path("api/devices/<str:device_id>/radar-rules/", views.radar_rules),
    path("api/devices/<str:device_id>/params/", views.params),
    # Dashboard files. Fine for the lab prototype; a production server would
    # serve frontend/ directly (e.g. nginx) and proxy /api/ to Django.
    path("", serve, {"path": "index.html", "document_root": settings.FRONTEND_DIR}),
    re_path(r"^(?P<path>(?:css|js)/[\w.-]+)$", serve, {"document_root": settings.FRONTEND_DIR}),
]
