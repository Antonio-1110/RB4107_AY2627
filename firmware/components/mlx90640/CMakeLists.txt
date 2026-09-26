idf_component_register(SRCS "vendor/MLX90640_API.c"
                            "src/mlx90640_i2c_esp.c"
                            "src/mlx90640.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "vendor"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_timer)

# Upstream code, kept unmodified.
set_source_files_properties("vendor/MLX90640_API.c" PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-maybe-uninitialized")
