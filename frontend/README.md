# Monitoring dashboard

Plain HTML, CSS and JavaScript modules, with no build step and no dependencies.
It polls the Django API in [`django/`](../django) for data. It can send two
kinds of commands to the controller through that API: the **Reset shutdown**
button ([remote_reset.md](../docs/remote_reset.md)) and C4002 radar tuning
([c4002_tuning.md](../docs/c4002_tuning.md)). It never trips a shutdown,
silences the buzzer or drives the relay directly. There is no login, so anyone
who can open the page can use both.

```text
index.html        page layout: overview and stall detail views
css/dashboard.css styling
js/config.js      API base URL, poll interval, request timeout
js/api.js         the API calls: GETs for devices, history, events, thermal_frame, C4002 live;
                  POSTs for the reset and C4002 commands
js/dashboard.js   state, polling and rendering of both views
js/chart.js       temperature history canvas
js/heatmap.js     thermal camera heat map canvas
js/reset.js       "Reset shutdown" button and its status
js/tuning.js      C4002 radar tuning panel
js/live_radar.js  live radar view of one presence node (gates over the last minute)
js/dom.js         small DOM helpers
js/format.js      value formatting
```

## Running it

Start the backend (`python manage.py runserver` in `django/`, see its
README, or `tools/run_dev.sh`) and open http://127.0.0.1:8000/, or
http://192.168.1.50:8000/ from another machine on the network. Django serves
this folder at `/`, so the page and the API share one origin and port. Opening
`index.html` straight from disk does not work, because browsers block
JavaScript modules and API calls from `file://` pages.

For a demo without hardware, run `python manage.py simulate_fleet --direct`
with `RB4107_LOCATION_CATALOG_FILE=locations.demo.json`.

## Where the data comes from

The API fields and how they map from the firmware's MQTT messages are listed in
[`docs/dashboard.md`](../docs/dashboard.md). Format the code with Prettier
(`npx prettier --print-width 100 --write frontend`) before committing.
