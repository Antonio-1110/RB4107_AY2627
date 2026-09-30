"""Isolated TCP MQTT broker -> existing subscriber -> database -> real HTTP API.

Run with the backend virtualenv from any directory. Creates only temporary data.
Set RB4107_BROWSER_CHECK=1 for the optional Playwright UI check.
"""
import copy
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT / "backend/django"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def eventually(check, timeout=20):
    until = time.monotonic() + timeout
    last = None
    while time.monotonic() < until:
        try:
            result = check()
            if result:
                return result
        except Exception as error:
            last = error
        time.sleep(.1)
    raise AssertionError(f"Condition timed out: {last}")


def main():
    import paho.mqtt.client as mqtt
    broker_port, http_port = free_port(), free_port()
    processes, publisher = [], None
    with tempfile.TemporaryDirectory(prefix="rb4107-dashboard-") as tmp:
        env = {**os.environ, "RB4107_SQLITE_PATH": str(Path(tmp) / "smoke.sqlite3"),
               "RB4107_MQTT_HOST": "127.0.0.1", "RB4107_MQTT_PORT": str(broker_port),
               "RB4107_MQTT_TOPIC": "rb4107/+/#", "RB4107_MQTT_USERNAME": "", "RB4107_MQTT_PASSWORD": "",
               "RB4107_MQTT_CLIENT_ID": "rb4107-isolated-smoke", "RB4107_LOG_LEVEL": "WARNING",
               "DJANGO_ALLOWED_HOSTS": "127.0.0.1,localhost", "DJANGO_DEBUG": "1",
               "RB4107_LOCATION_CATALOG_FILE": str(BACKEND / "location_catalog.json")}
        with open(Path(tmp) / "process.log", "w+") as logs:
            def start(args):
                proc = subprocess.Popen([sys.executable, *args], cwd=BACKEND, env=env, stdout=logs, stderr=logs)
                processes.append(proc)
                return proc

            def get(path):
                with urlopen(f"http://127.0.0.1:{http_port}{path}", timeout=3) as response:
                    return json.load(response)

            def connected():
                return get("/api/health/")["worker"]["connected"]

            def latest():
                return get("/api/devices/controller_01/latest/")

            try:
                subprocess.run([sys.executable, "manage.py", "migrate", "--noinput"], cwd=BACKEND, env=env,
                               check=True, stdout=logs, stderr=logs)
                broker = start([str(Path(__file__).resolve()), "--broker", str(broker_port)])
                start(["manage.py", "runserver", f"127.0.0.1:{http_port}", "--noreload"])
                worker = start(["manage.py", "mqtt_subscriber", "--stats-interval", "0"])
                eventually(connected)
                publisher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
                publisher.connect("127.0.0.1", broker_port)
                publisher.loop_start()

                def publish(suffix, data, retain=False):
                    raw = data if isinstance(data, bytes) else json.dumps(data)
                    result = publisher.publish("rb4107/controller_01/" + suffix, raw, qos=1, retain=retain)
                    result.wait_for_publish(5)
                    assert result.is_published()

                samples = [json.loads(line) for line in (BACKEND / "ingest/tests/firmware_samples.jsonl").read_text().splitlines()]
                for sample in samples:
                    publish(sample["topic"].removeprefix("rb4107/"), sample["payload"])
                eventually(lambda: len(get("/api/devices/controller_01/events/")["events"]) >= 4)
                assert latest()["values"]["temperature_c"] == 125
                assert len(get("/api/devices/controller_01/history/")["points"]) == 2
                print("PASS: real firmware fixtures through TCP MQTT, subscriber, SQLite, latest/history/events", flush=True)

                data = copy.deepcopy(next(s["payload"] for s in samples if s["payload"]["type"] == "telemetry"))
                data["safety"].update(state="WARNING", buzzer="WARNING", shutdown=False)
                publish("controller/state", data)
                eventually(lambda: latest()["values"]["safety_state"] == "warning")
                assert not latest()["values"]["manual_reset_required"]
                data["safety"].update(state="SHUTDOWN", buzzer="SHUTDOWN", shutdown=True)
                publish("controller/state", data, retain=True)
                eventually(lambda: latest()["values"].get("manual_reset_required") is True)
                publish("controller/state", b"not json")
                eventually(lambda: get("/api/health/")["invalid_messages_24h"] == 1)
                assert latest()["values"]["manual_reset_required"] is True
                print("PASS: warnings stay distinct from latched shutdown; invalid data is audited without altering state", flush=True)

                worker.terminate()
                worker.wait(8)
                eventually(lambda: not connected())
                assert latest()["display_state"]["last_known"]
                worker = start(["manage.py", "mqtt_subscriber"])
                eventually(connected)
                assert latest()["values"]["manual_reset_required"]
                print("PASS: subscriber restart preserves stored isolation and handles retained replay", flush=True)

                broker.terminate()
                broker.wait(5)
                eventually(lambda: not connected())
                assert latest()["values"]["manual_reset_required"]
                broker = start([str(Path(__file__).resolve()), "--broker", str(broker_port)])
                eventually(connected)
                eventually(publisher.is_connected)
                data["safety"].update(state="IDLE", buzzer="OFF", shutdown=False)
                publish("controller/state", data)
                eventually(lambda: latest()["values"]["manual_reset_required"] is False)
                print("PASS: broker outage/reconnect; fresh restored-supply report clears reset requirement", flush=True)

                simulator = start(["manage.py", "simulate_fleet"])
                eventually(lambda: len(get("/api/devices/")["devices"]) == 10)
                assert get("/api/devices/")["summary"]["total"] == 9
                if os.environ.get("RB4107_BROWSER_CHECK") == "1":
                    subprocess.run(["node", str(ROOT / "tools/dashboard_browser_check.cjs"),
                                    f"http://127.0.0.1:{http_port}"], env=env, check=True)
                print("PASS: schema-v2 MQTT fleet, separate controllers and shared-stall aggregation", flush=True)
            except Exception:
                logs.flush()
                logs.seek(0)
                print(logs.read()[-8000:], file=sys.stderr)
                raise
            finally:
                if publisher:
                    publisher.disconnect()
                    publisher.loop_stop()
                for proc in reversed(processes):
                    if proc.poll() is None:
                        proc.terminate()
                        try:
                            proc.wait(8)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--broker":
        import asyncio
        from amqtt.broker import Broker

        async def serve():
            broker = Broker({"listeners": {"default": {"type": "tcp", "bind": f"127.0.0.1:{sys.argv[2]}"}},
                             "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}}})
            await broker.start()
            await asyncio.Event().wait()

        asyncio.run(serve())
    else:
        main()
