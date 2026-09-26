idf_component_register(SRCS "src/rb_wallclock.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES pcf85063 rb_board_s3 rb_config rb_time esp_netif)

# Build time, used only by the RB_TIME_SET_RTC_FROM_BUILD bench option.
string(TIMESTAMP RB_BUILD_EPOCH "%s" UTC)
target_compile_definitions(${COMPONENT_LIB} PRIVATE RB_BUILD_EPOCH=${RB_BUILD_EPOCH})
