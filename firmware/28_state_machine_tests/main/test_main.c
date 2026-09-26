/*
 * TODO section 28: state-machine testing without hardware.
 *
 * Runs everywhere:
 *   ESP32-S3:  idf.py build flash monitor
 *   host (PC): idf.py --preview set-target linux && idf.py build && ./build/rb4107_28_state_machine_tests.elf
 *              (or tools/run_host_tests.sh)
 *
 * All of the logic under test is plain C with timestamps passed in, so the
 * tests run a simulated clock and finish in milliseconds.
 */
#include <stdio.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "unity.h"

void run_safety_tests(void);
void run_sensor_node_tests(void);
void run_fault_tests(void);
void run_protocol_tests(void);
void run_c4002_tests(void);
void run_thermal_tests(void);
void run_json_tests(void);
void run_kconfig_tests(void);

void setUp(void) {}
void tearDown(void) {}

void app_main(void)
{
    UNITY_BEGIN();
    run_safety_tests();
    run_sensor_node_tests();
    run_fault_tests();
    run_protocol_tests();
    run_c4002_tests();
    run_thermal_tests();
    run_json_tests();
    run_kconfig_tests();
    const int failures = UNITY_END();
#if CONFIG_IDF_TARGET_LINUX
    exit(failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
#else
    printf(failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
#endif
}
