# Header-only component. Its Kconfig is the single place where every RB4107
# tunable lives (TODO section 26); all firmware projects depend on it.
idf_component_register(INCLUDE_DIRS "include")
