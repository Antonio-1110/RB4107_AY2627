# Firmware

Every numbered folder in here is a standalone ESP-IDF project for one section of
[`TODO.md`](../TODO.md). They share code through [`components/`](components):
each project's `CMakeLists.txt` adds `../components` to `EXTRA_COMPONENT_DIRS`
and builds only the components its `main` requires.

Build any project the usual way:

```bash
cd firmware/<NN_project>
idf.py build            # the target comes from sdkconfig.defaults
idf.py -p <PORT> flash monitor
idf.py menuconfig       # "RB4107 configuration" menu holds all tunables
```

See the root [`README.md`](../README.md) for the section → folder map.
