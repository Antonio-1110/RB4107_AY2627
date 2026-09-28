#include "rb_diag.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "fault_manager.h"
#include "rb_config.h"
#include "rb_controller.h"
#include "rb_espnow.h"
#include "rb_mqtt.h"
#include "rb_net.h"
#include "rb_safety_config.h"
#include "rb_sim.h"
#include "rb_telemetry.h"
#include "rb_time.h"

static const char *TAG = "DIAG";

#if CONFIG_RB_DIAG_TEST_TIMERS_AT_BOOT
static bool s_test_timers = true;
#else
static bool s_test_timers = false;
#endif

safety_config_t rb_diag_test_safety_config(void)
{
    safety_config_t cfg = rb_safety_config_from_kconfig();
    cfg.warning_timeout_ms = CONFIG_RB_DIAG_TEST_WARNING_S * 1000u;
    cfg.shutdown_timeout_ms = CONFIG_RB_DIAG_TEST_SHUTDOWN_S * 1000u;
    cfg.fault_shutdown_timeout_ms = CONFIG_RB_DIAG_TEST_FAULT_SHUTDOWN_S * 1000u;
    return cfg;
}

/* ---- print commands ---- */

static int cmd_presence(int argc, char **argv)
{
    rb_snapshot_t s;
    rb_controller_get_snapshot(&s);
    printf("presence (combined, used by the state machine): %s\n", rb_tristate_presence_name(s.inputs.presence));
    for (int slot = NODE_SLOT_PRESENCE_A; slot <= NODE_SLOT_PRESENCE_B; slot++) {
        const sensor_node_state_t *n = &s.nodes.nodes[slot];
        if (!s.nodes.enabled[slot]) {
            printf("  %-10s not configured\n", node_slot_name(slot));
            continue;
        }
        const presence_reading_t *p = &n->presence;
        printf("  %-10s node_%02" PRIu32 ": %s (valid=%d moving=%d stationary=%d distance=%.2f m, node ts %" PRIu32
               " ms)\n",
               node_slot_name(slot), n->node_id, rb_tristate_presence_name(sensor_node_presence(n)), n->sensor_ok,
               p->moving_target, p->stationary_target, p->distance_m, p->timestamp_ms);
    }
    return 0;
}

static int cmd_thermal(int argc, char **argv)
{
    rb_snapshot_t s;
    rb_controller_get_snapshot(&s);
    const sensor_node_state_t *n = &s.nodes.nodes[NODE_SLOT_THERMAL];
    const thermal_reading_t *t = &n->thermal;
    printf("thermal node_%02" PRIu32 ": %s max=%.1f min=%.1f mean=%.1f hot-region=%.1f C rate=%.2f C/min px>thr=%u\n",
           n->node_id, n->sensor_ok ? "valid" : "INVALID", t->max_temp_c, t->min_temp_c, t->mean_temp_c,
           t->hot_region_temp_c, t->temp_rate_c_per_min, t->pixels_above_threshold);
    return 0;
}

static int cmd_espnow(int argc, char **argv)
{
    rb_espnow_rx_stats_t rx;
    rb_espnow_get_rx_stats(&rx);
    rb_snapshot_t s;
    rb_controller_get_snapshot(&s);
    printf("espnow rx: ok=%" PRIu32 " bad len=%" PRIu32 " magic=%" PRIu32 " version=%" PRIu32 " type=%" PRIu32
           " crc=%" PRIu32 " role=%" PRIu32 " queue overflow=%" PRIu32 " | unknown node=%" PRIu32 "\n",
           rx.received, rx.bad_length, rx.bad_magic, rx.bad_version, rx.bad_type, rx.bad_crc, rx.bad_role,
           rx.queue_overflow, s.nodes.unknown_node);
    for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
        if (!s.nodes.enabled[slot]) {
            continue;
        }
        const sensor_node_state_t *n = &s.nodes.nodes[slot];
        printf("  %-10s node_%02" PRIu32 ": packets=%" PRIu32 " last seq=%" PRIu32 " (%" PRIu32 " ms ago) missed=%" PRIu32
               " dup=%" PRIu32 " out-of-order=%" PRIu32 " restarts=%" PRIu32 " wrong-role=%" PRIu32 "\n",
               node_slot_name(slot), n->node_id, n->packets, n->last_sequence,
               n->packets ? rb_time_mono_ms() - n->last_received_ms : 0, n->missed, n->duplicates, n->out_of_order,
               n->restarts, n->wrong_role);
    }
    return 0;
}

static int cmd_node(int argc, char **argv)
{
    rb_snapshot_t s;
    rb_controller_get_snapshot(&s);
    for (int slot = 0; slot < NODE_SLOT_COUNT; slot++) {
        const sensor_node_state_t *n = &s.nodes.nodes[slot];
        if (!s.nodes.enabled[slot]) {
            printf("%-10s not configured\n", node_slot_name(slot));
            continue;
        }
        printf("%-10s node_%02" PRIu32 ": link %s, %s reading %s, node fault flags 0x%04x\n", node_slot_name(slot),
               n->node_id, node_link_state_name(n->link), rb_node_role_name(n->role),
               n->sensor_ok ? "valid" : "INVALID", n->node_fault_flags);
    }
    return 0;
}

static int cmd_safety(int argc, char **argv)
{
    rb_snapshot_t s;
    rb_controller_get_snapshot(&s);
    printf("safety: %s for %" PRIu32 " ms (last: %s), unattended %" PRIu32 " ms, buzzer %s, shutdown %s, timers %s, "
           "loop %" PRIu32 "\n",
           safety_state_name(s.state), s.state_duration_ms, s.last_reason ? s.last_reason : "-", s.unattended_ms,
           safety_buzzer_name(s.outputs.buzzer), s.outputs.shutdown ? "ACTIVE" : "released",
           s.test_timers ? "TEST" : "normal", s.loop_count);
    return 0;
}

static int cmd_faults(int argc, char **argv)
{
    bool any = false;
    for (int id = 0; id < FAULT_COUNT; id++) {
        fault_status_t st;
        fault_get_status(id, &st);
        if (st.active) {
            const fault_info_t *info = fault_info(id);
            printf("  %-26s %-9s since %" PRIu32 " ms, raised %" PRIu32 "x, detail %" PRId32 "\n", info->name,
                   fault_class_name(info->fault_class), st.since_ms, st.raise_count, st.detail);
            any = true;
        }
    }
    if (!any) {
        printf("no active faults\n");
    }
    return 0;
}

static int cmd_mqtt(int argc, char **argv)
{
    rb_mqtt_stats_t m;
    rb_mqtt_get_stats(&m);
    rb_telemetry_stats_t t;
    rb_telemetry_get_stats(&t);
    const esp_ip4_addr_t ip = rb_net_ip();
    printf("network: %s %s, IP " IPSTR "\n", rb_net_interface_name(), rb_net_state_name(rb_net_state()), IP2STR(&ip));
    printf("mqtt: %s (broker %s:%d), published=%" PRIu32 " dropped=%" PRIu32 " connects=%" PRIu32
           " disconnects=%" PRIu32 " | telemetry periodic=%" PRIu32 " events=%" PRIu32 "\n",
           rb_mqtt_state_name(rb_mqtt_state()), CONFIG_RB_BROKER_HOST, CONFIG_RB_BROKER_PORT, m.published, m.dropped,
           m.connects, m.disconnects, t.periodic, t.events);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    cmd_safety(0, NULL);
    cmd_node(0, NULL);
    cmd_presence(0, NULL);
    cmd_thermal(0, NULL);
    cmd_mqtt(0, NULL);
    printf("faults:\n");
    cmd_faults(0, NULL);
    return 0;
}

/* ---- control commands ---- */

/* "a" / "b" / "thermal" / "all" -> sim node mask. */
static int parse_nodes(int argc, char **argv, int idx)
{
    if (argc <= idx || strcmp(argv[idx], "all") == 0) {
        return (1 << RB_SIM_NODE_COUNT) - 1;
    }
    if (strcmp(argv[idx], "a") == 0) {
        return 1 << RB_SIM_PRESENCE_A;
    }
    if (strcmp(argv[idx], "b") == 0) {
        return 1 << RB_SIM_PRESENCE_B;
    }
    if (strcmp(argv[idx], "thermal") == 0) {
        return 1 << RB_SIM_THERMAL;
    }
    if (strcmp(argv[idx], "presence") == 0) {
        return (1 << RB_SIM_PRESENCE_A) | (1 << RB_SIM_PRESENCE_B);
    }
    return 0;
}

static void set_nodes(bool *field, int mask, bool value)
{
    for (int i = 0; i < RB_SIM_NODE_COUNT; i++) {
        if (mask & (1 << i)) {
            field[i] = value;
        }
    }
}

static int cmd_sim(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: sim on | present | absent | temp <degC> [rate C/min]\n"
               "       sim node-off|node-on|invalid|valid [a|b|thermal|presence|all]   (default all)\n");
        return 1;
    }
#if CONFIG_RB_SIM_NODE
    static bool started = true; /* started at boot by rb_controller_app */
#else
    static bool started = false;
#endif
    rb_sim_inputs_t in;
    rb_sim_get(&in);
    const char *a = argv[1];
    if (strcmp(a, "on") == 0) {
        if (!started) {
            const rb_controller_config_t cfg = rb_controller_config_from_kconfig();
            printf("starting simulated nodes: power off the real sensor nodes, they use the same node IDs\n");
            rb_sim_start(cfg.nodes.presence_node_ids, cfg.nodes.presence_node_count, cfg.nodes.thermal_node_id, 500);
            started = true;
            return 0;
        }
        set_nodes(in.online, parse_nodes(argc, argv, 2), true);
    } else if (!started) {
        printf("simulation not running (use 'sim on')\n");
        return 1;
    } else if (strcmp(a, "present") == 0) {
        in.person_present = true;
    } else if (strcmp(a, "absent") == 0) {
        in.person_present = false;
    } else if (strcmp(a, "temp") == 0 && argc >= 3) {
        in.hot_region_c = strtof(argv[2], NULL);
        in.rate_c_per_min = argc >= 4 ? strtof(argv[3], NULL) : 0.0f;
    } else if (strcmp(a, "node-off") == 0 || strcmp(a, "off") == 0 || strcmp(a, "node-on") == 0 ||
               strcmp(a, "invalid") == 0 || strcmp(a, "valid") == 0) {
        const int mask = parse_nodes(argc, argv, 2);
        if (mask == 0) {
            printf("unknown node '%s' (a, b, thermal, presence or all)\n", argv[2]);
            return 1;
        }
        if (strcmp(a, "invalid") == 0 || strcmp(a, "valid") == 0) {
            set_nodes(in.valid, mask, strcmp(a, "valid") == 0);
        } else {
            set_nodes(in.online, mask, strcmp(a, "node-on") == 0);
        }
    } else {
        printf("unknown sim command '%s'\n", a);
        return 1;
    }
    rb_sim_set(&in);
    static const char *const NAMES[] = {"presence_a", "presence_b", "thermal"};
    for (int i = 0; i < RB_SIM_NODE_COUNT; i++) {
        printf("sim %-10s %s, reading %s\n", NAMES[i], in.online[i] ? "online" : "OFF", in.valid[i] ? "valid" : "INVALID");
    }
    printf("sim person %s, hot region %.1f C (%.1f C/min)\n", in.person_present ? "present" : "absent", in.hot_region_c,
           in.rate_c_per_min);
    return 0;
}

static int cmd_timers(int argc, char **argv)
{
    if (argc < 2 || (strcmp(argv[1], "test") != 0 && strcmp(argv[1], "normal") != 0)) {
        printf("usage: timers test|normal   (now: %s)\n", s_test_timers ? "test" : "normal");
        return 1;
    }
    s_test_timers = strcmp(argv[1], "test") == 0;
    const safety_config_t cfg = s_test_timers ? rb_diag_test_safety_config() : rb_safety_config_from_kconfig();
    if (rb_controller_set_safety_config(&cfg, s_test_timers) != ESP_OK) {
        printf("rejected: inconsistent timer values\n");
        return 1;
    }
    printf("timers %s: warning %" PRIu32 " s, shutdown %" PRIu32 " s\n", argv[1], cfg.warning_timeout_ms / 1000,
           cfg.shutdown_timeout_ms / 1000);
    return 0;
}

static int cmd_reset(int argc, char **argv)
{
    rb_controller_request_reset();
    printf("reset requested\n");
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    static const char *const names[] = {"none", "error", "warn", "info", "debug", "verbose"};
    if (argc == 3) {
        for (int lvl = 0; lvl < 6; lvl++) {
            if (strcmp(argv[2], names[lvl]) == 0) {
                esp_log_level_set(argv[1], (esp_log_level_t)lvl);
                printf("log %s -> %s\n", argv[1], names[lvl]);
                return 0;
            }
        }
    }
    printf("usage: log <TAG|*> <none|error|warn|info|debug|verbose>\n");
    return 1;
}

static void add(const char *name, const char *help, esp_console_cmd_func_t fn)
{
    const esp_console_cmd_t cmd = {.command = name, .help = help, .func = fn};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

esp_err_t rb_diag_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "rb4107>";
    repl_cfg.task_priority = 2; /* below the safety task, always */
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    const esp_console_dev_uart_config_t hw = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw, &repl_cfg, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    const esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw, &repl_cfg, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    const esp_console_dev_usb_cdc_config_t hw = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw, &repl_cfg, &repl));
#endif
    esp_console_register_help_command();
    add("status", "Everything below in one go", cmd_status);
    add("presence", "Combined presence and each presence node's reading", cmd_presence);
    add("thermal", "Latest thermal features from the thermal node", cmd_thermal);
    add("espnow", "ESP-NOW receive statistics and per-node sequence tracking", cmd_espnow);
    add("node", "Health of every sensor node", cmd_node);
    add("safety", "Safety state, timers and outputs", cmd_safety);
    add("faults", "Active faults", cmd_faults);
    add("mqtt", "Network and MQTT status", cmd_mqtt);
    add("sim", "Simulated sensor nodes (see 'sim' for usage)", cmd_sim);
    add("timers", "timers test|normal: accelerated safety timers", cmd_timers);
    add("reset", "Operator reset / acknowledge", cmd_reset);
    add("log", "log <TAG|*> <level>: change log verbosity", cmd_log);
    ESP_LOGI(TAG, "diagnostic console ready; type 'help'");
    return esp_console_start_repl(repl);
}
