"""Read-only telemetry API, plus the dashboard from the repository's frontend/ folder."""
from django.conf import settings
from django.urls import path, re_path
from django.views.static import serve

from ingest import views

urlpatterns = [
    path("api/health/", views.health),
    path("api/devices/", views.devices),
    path("api/devices/<str:device_id>/latest/", views.latest),
    path("api/devices/<str:device_id>/history/", views.history),
    path("api/devices/<str:device_id>/events/", views.events),
    # Dashboard files. Fine for the lab prototype; a production server would
    # serve frontend/ directly (e.g. nginx) and proxy /api/ to Django.
    path("", serve, {"path": "index.html", "document_root": settings.FRONTEND_DIR}),
    re_path(r"^(?P<path>(?:css|js)/[\w.-]+)$", serve, {"document_root": settings.FRONTEND_DIR}),
]
