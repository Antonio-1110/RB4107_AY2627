# Monitoring dashboard

Plain HTML, CSS and JavaScript modules, with no build step and no dependencies.
It only reads data: it polls the Django API in [`django/`](../django)
and can not operate, silence or reset anything on the controller.

```text
index.html        page layout: overview and stall detail views
css/dashboard.css styling
js/config.js      API base URL, poll interval, request timeout
js/api.js         the GET calls the page makes (/api/devices/, history, events, thermal_frame)
js/dashboard.js   state, polling and rendering of both views
js/chart.js       temperature history canvas
js/heatmap.js     thermal camera heat map canvas
js/dom.js         small DOM helpers
js/format.js      value formatting
```

## Running it

Start the backend (`python manage.py runserver` in `django/`, see its
README) and open http://127.0.0.1:8000/. Django serves this folder at `/`, so
the page and the API share one origin. Opening `index.html` straight from disk
does not work, because browsers block JavaScript modules and API calls from
`file://` pages.

For a demo without hardware, run `python manage.py simulate_fleet --direct`
with `RB4107_LOCATION_CATALOG_FILE=locations.demo.json`.

## Where the data comes from

The API fields and how they map from the firmware's MQTT messages are listed in
[`docs/dashboard.md`](../docs/dashboard.md). Format the code with Prettier
(`npx prettier --print-width 100 --write frontend`) before committing.
