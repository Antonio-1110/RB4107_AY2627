"""Same-origin, read-only dashboard and telemetry API."""
from django.urls import path
from ingest import views

urlpatterns = [
    path("", views.dashboard),
    path("api/health/", views.health),
    path("api/devices/", views.devices),
    path("api/devices/<str:device_id>/latest/", views.latest),
    path("api/devices/<str:device_id>/history/", views.history),
    path("api/devices/<str:device_id>/events/", views.events),
]
