#include "rb_controller_app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "fault_manager.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rb_app_faults.h"
#include "rb_c4002_relay.h"
#include "rb_config.h"
#include "rb_connectivity.h"
#include "rb_continuity.h"
#include "rb_controller.h"
#include "rb_ctrl_cmd.h"
#include "rb_diag.h"
#include "rb_espnow.h"
#include "rb_heatmap.h"
#include "rb_log.h"
#if CONFIG_RB_MQTT_PUBLISH_LOG
#include "rb_log_capture.h"
#endif
#include "rb_mqtt.h"
#include "rb_outputs.h"
#include "rb_sim.h"
#include "rb_telemetry.h"
#include "rb_time.h"
#include "rb_topics.h"
#include "rb_wallclock.h"
#if CONFIG_RB_CTRL_VALVE_WIRELESS
#include "rb_valve_link.h"
#endif

static const char *TAG = "CONTROLLER";

static void apply_outputs(const safety_outputs_t *out, void *ctx)
{
    rb_outputs_apply(out);
#if CONFIG_RB_CTRL_VALVE_WIRELESS
    rb_valve_link_set_shutdown(out->shutdown);
#endif
}

#if CONFIG_RB_CTRL_REMOTE_COOKING_TEMP || CONFIG_RB_CTRL_REMOTE_COOKWARE || CONFIG_RB_CTRL_REMOTE_TIERS || \
    CONFIG_RB_CTRL_REMOTE_ATTENDANCE || CONFIG_RB_CTRL_REMOTE_RADAR_RULES || CONFIG_RB_CTRL_REMOTE_PARAMS
/* The dashboard's cooking temperature, cookware profile and temperature tiers, saved so they survive a reboot. */
#define NVS_NAMESPACE "rb_safety"
#define NVS_KEY_COOKING "cook_dc"
#define NVS_KEY_COOKWARE "cookware"
#define NVS_KEY_TIERS "tiers"
#define NVS_KEY_ATTENDANCE "attend"
#define NVS_KEY_RADAR "radar"
#define NVS_KEY_PARAMS "params"

static bool open_nvs(nvs_open_mode_t mode, nvs_handle_t *h)
{
    esp_err_t err = nvs_flash_init(); /* also done by Wi-Fi start; a second call is harmless */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err == ESP_OK && nvs_open(NVS_NAMESPACE, mode, h) == ESP_OK;
}

#endif

#if CONFIG_RB_CTRL_REMOTE_COOKING_TEMP
static void cooking_temp_load(void)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READONLY, &h)) {
        return; /* nothing saved yet */
    }
    int16_t dc;
    if (nvs_get_i16(h, NVS_KEY_COOKING, &dc) == ESP_OK && dc >= 0 && dc <= RB_CTRL_COOKING_TEMP_MAX_DC) {
        rb_controller_set_cooking_temp(dc / 10.0f);
        ESP_LOGW(TAG, "cooking temperature %.1f C (saved from the dashboard)", dc / 10.0f);
    }
    nvs_close(h);
}

/* erase: forget the saved value (back to menuconfig at the next boot too). */
static void cooking_temp_save(bool erase, int16_t dc)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READWRITE, &h)) {
        ESP_LOGW(TAG, "NVS not available: the cooking temperature lasts until the next reboot");
        return;
    }
    esp_err_t err = erase ? nvs_erase_key(h, NVS_KEY_COOKING) : nvs_set_i16(h, NVS_KEY_COOKING, dc);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; /* nothing was saved */
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cooking temperature not saved (%s): it lasts until the next reboot", esp_err_to_name(err));
    }
}
#endif

#if CONFIG_RB_CTRL_REMOTE_COOKWARE
static void cookware_load(void)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READONLY, &h)) {
        return;
    }
    uint8_t profile;
    if (nvs_get_u8(h, NVS_KEY_COOKWARE, &profile) == ESP_OK && profile < SAFETY_COOKWARE_COUNT) {
        rb_controller_set_cookware((safety_cookware_t)profile);
        ESP_LOGW(TAG, "cookware profile %s (saved from the dashboard)", safety_cookware_name((safety_cookware_t)profile));
    }
    nvs_close(h);
}

/* erase: forget the saved profile (back to menuconfig at the next boot too). */
static void cookware_save(bool erase, uint8_t profile)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READWRITE, &h)) {
        ESP_LOGW(TAG, "NVS not available: the cookware profile lasts until the next reboot");
        return;
    }
    esp_err_t err = erase ? nvs_erase_key(h, NVS_KEY_COOKWARE) : nvs_set_u8(h, NVS_KEY_COOKWARE, profile);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cookware profile not saved (%s): it lasts until the next reboot", esp_err_to_name(err));
    }
}

/* MQTT task: the dashboard's cookware profile, or back to menuconfig. */
static void set_cookware(const rb_ctrl_cmd_t *cmd)
{
    safety_cookware_t profile = SAFETY_COOKWARE_COUNT;
    const bool set = cmd->action == RB_CTRL_ACTION_COOKWARE && safety_cookware_from_name(cmd->cookware, &profile);
    ESP_LOGW(TAG, "cookware profile %s from the dashboard (request %u)", set ? cmd->cookware : "menuconfig default",
             (unsigned)cmd->request_id);
    rb_controller_set_cookware(set ? profile : SAFETY_COOKWARE_COUNT);
    cookware_save(!set, (uint8_t)profile);
}
#endif

#if CONFIG_RB_CTRL_REMOTE_TIERS
typedef struct {
    uint8_t version; /* 1 */
    rb_tiers_t tiers;
} saved_tiers_t;

static void tiers_load(void)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READONLY, &h)) {
        return;
    }
    saved_tiers_t saved;
    size_t len = sizeof(saved);
    if (nvs_get_blob(h, NVS_KEY_TIERS, &saved, &len) == ESP_OK && len == sizeof(saved) && saved.version == 1) {
        rb_controller_set_tiers(&saved.tiers);
        ESP_LOGW(TAG, "temperature tiers saved from the dashboard: from %.0f C %lu/%lu s, from %.0f C %lu/%lu s",
                 (double)saved.tiers.tier1_temp_c, (unsigned long)(saved.tiers.tier1_warning_ms / 1000),
                 (unsigned long)(saved.tiers.tier1_shutdown_ms / 1000), (double)saved.tiers.tier2_temp_c,
                 (unsigned long)(saved.tiers.tier2_warning_ms / 1000), (unsigned long)(saved.tiers.tier2_shutdown_ms / 1000));
    }
    nvs_close(h);
}

/* tiers == NULL: forget the saved tiers (back to menuconfig at the next boot too). */
static void tiers_save(const rb_tiers_t *tiers)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READWRITE, &h)) {
        ESP_LOGW(TAG, "NVS not available: the temperature tiers last until the next reboot");
        return;
    }
    esp_err_t err;
    if (tiers == NULL) {
        err = nvs_erase_key(h, NVS_KEY_TIERS);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        const saved_tiers_t saved = {.version = 1, .tiers = *tiers};
        err = nvs_set_blob(h, NVS_KEY_TIERS, &saved, sizeof(saved));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "temperature tiers not saved (%s): they last until the next reboot", esp_err_to_name(err));
    }
}

/* MQTT task: the dashboard's temperature tiers, or back to menuconfig. */
static void set_tiers(const rb_ctrl_cmd_t *cmd)
{
    if (cmd->action == RB_CTRL_ACTION_TIERS_DEFAULTS) {
        ESP_LOGW(TAG, "temperature tiers: menuconfig values (request %u)", (unsigned)cmd->request_id);
        rb_controller_set_tiers(NULL);
        tiers_save(NULL);
        return;
    }
    const rb_tiers_t tiers = {
        .tier1_temp_c = cmd->tier1_temp_dc / 10.0f,
        .tier1_warning_ms = cmd->tier1_warning_ms,
        .tier1_shutdown_ms = cmd->tier1_shutdown_ms,
        .tier2_temp_c = cmd->tier2_temp_dc / 10.0f,
        .tier2_rate_c_per_min = cmd->tier2_rate_dc_per_min / 10.0f,
        .tier2_rate_min_temp_c = cmd->tier2_rate_min_temp_dc / 10.0f,
        .tier2_warning_ms = cmd->tier2_warning_ms,
        .tier2_shutdown_ms = cmd->tier2_shutdown_ms,
    };
    ESP_LOGW(TAG, "temperature tiers from the dashboard (request %u)", (unsigned)cmd->request_id);
    rb_controller_set_tiers(&tiers); /* the safety task refuses inconsistent values and keeps the ones in use */
    tiers_save(&tiers);
}
#endif

#if CONFIG_RB_CTRL_REMOTE_ATTENDANCE
typedef struct {
    uint8_t version; /* 1 */
    rb_attendance_t attendance;
} saved_attendance_t;

static void attendance_load(void)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READONLY, &h)) {
        return;
    }
    saved_attendance_t saved;
    size_t len = sizeof(saved);
    if (nvs_get_blob(h, NVS_KEY_ATTENDANCE, &saved, &len) == ESP_OK && len == sizeof(saved) && saved.version == 1) {
        rb_controller_set_attendance(&saved.attendance);
        ESP_LOGW(TAG, "attendance saved from the dashboard: %s radars, pause budget %lu s / %u %%",
                 saved.attendance.fusion_all ? "all" : "either of the",
                 (unsigned long)(saved.attendance.pause_budget_ms / 1000), (unsigned)saved.attendance.pause_budget_pct);
    }
    nvs_close(h);
}

/* attendance == NULL: forget the saved values (back to menuconfig at the next boot too). */
static void attendance_save(const rb_attendance_t *attendance)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READWRITE, &h)) {
        ESP_LOGW(TAG, "NVS not available: the attendance settings last until the next reboot");
        return;
    }
    esp_err_t err;
    if (attendance == NULL) {
        err = nvs_erase_key(h, NVS_KEY_ATTENDANCE);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        const saved_attendance_t saved = {.version = 1, .attendance = *attendance};
        err = nvs_set_blob(h, NVS_KEY_ATTENDANCE, &saved, sizeof(saved));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "attendance settings not saved (%s): they last until the next reboot", esp_err_to_name(err));
    }
}

#if CONFIG_RB_CTRL_REMOTE_RADAR_RULES
typedef struct {
    uint8_t version; /* 1 */
    rb_radar_rules_t rules;
} saved_radar_t;

static void radar_rules_load(void)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READONLY, &h)) {
        return;
    }
    saved_radar_t saved;
    size_t len = sizeof(saved);
    if (nvs_get_blob(h, NVS_KEY_RADAR, &saved, &len) == ESP_OK && len == sizeof(saved) && saved.version == 1) {
        rb_controller_set_radar_rules(&saved.rules);
        ESP_LOGW(TAG, "radar rules saved from the dashboard: %s", radar_rules_mode_name(saved.rules.mode));
    }
    nvs_close(h);
}

/* rules == NULL: forget the saved values (back to menuconfig at the next boot too). */
static void radar_rules_save(const rb_radar_rules_t *rules)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READWRITE, &h)) {
        ESP_LOGW(TAG, "NVS not available: the radar rules last until the next reboot");
        return;
    }
    esp_err_t err;
    if (rules == NULL) {
        err = nvs_erase_key(h, NVS_KEY_RADAR);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        const saved_radar_t saved = {.version = 1, .rules = *rules};
        err = nvs_set_blob(h, NVS_KEY_RADAR, &saved, sizeof(saved));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "radar rules not saved (%s): they last until the next reboot", esp_err_to_name(err));
    }
}

/* MQTT task: the dashboard's radar rules, or back to menuconfig. */
static void set_radar_rules(const rb_ctrl_cmd_t *cmd)
{
    if (cmd->action == RB_CTRL_ACTION_RADAR_RULES_DEFAULTS) {
        ESP_LOGW(TAG, "radar rules: menuconfig values (request %u)", (unsigned)cmd->request_id);
        rb_controller_set_radar_rules(NULL);
        radar_rules_save(NULL);
        return;
    }
    rb_radar_rules_t rules = {
        .mode = strcmp(cmd->radar_mode, "enforce") == 0   ? RADAR_RULES_ENFORCE
                : strcmp(cmd->radar_mode, "observe") == 0 ? RADAR_RULES_OBSERVE
                                                          : RADAR_RULES_OFF,
        .rules = {.quiet_gap_ms = cmd->radar_quiet_gap_ms,
                  .dwell_ms = cmd->radar_dwell_ms,
                  .stationary_min_ms = cmd->radar_stationary_min_ms,
                  .settle_window_ms = cmd->radar_settle_window_ms,
                  .settle_span_cm = cmd->radar_settle_span_cm,
                  .zone_max_cm = cmd->radar_zone_max_cm},
    };
    ESP_LOGW(TAG, "radar rules from the dashboard (request %u)", (unsigned)cmd->request_id);
    rb_controller_set_radar_rules(&rules);
    radar_rules_save(&rules);
}
#endif

/* MQTT task: the dashboard's fusion and pause budget, or back to menuconfig. */
static void set_attendance(const rb_ctrl_cmd_t *cmd)
{
    if (cmd->action == RB_CTRL_ACTION_ATTENDANCE_DEFAULTS) {
        ESP_LOGW(TAG, "attendance: menuconfig values (request %u)", (unsigned)cmd->request_id);
        rb_controller_set_attendance(NULL);
        attendance_save(NULL);
        return;
    }
    const rb_attendance_t attendance = {
        .pause_budget_ms = cmd->pause_budget_ms,
        .pause_budget_pct = cmd->pause_budget_pct,
        .fusion_all = cmd->fusion_all,
    };
    ESP_LOGW(TAG, "attendance from the dashboard (request %u)", (unsigned)cmd->request_id);
    rb_controller_set_attendance(&attendance);
    attendance_save(&attendance);
}
#endif

#if CONFIG_RB_CTRL_REMOTE_PARAMS
typedef struct {
    uint8_t version; /* 1 */
    uint32_t layout; /* rb_params_layout_hash() of the firmware that saved it */
    rb_params_t params;
} saved_params_t;

static void params_load(void)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READONLY, &h)) {
        return;
    }
    saved_params_t saved;
    size_t len = sizeof(saved);
    if (nvs_get_blob(h, NVS_KEY_PARAMS, &saved, &len) == ESP_OK) {
        if (len == sizeof(saved) && saved.version == 1 && saved.layout == rb_params_layout_hash() &&
            rb_params_in_range(&saved.params)) {
            rb_controller_set_params(&saved.params);
            ESP_LOGW(TAG, "other safety settings saved from the dashboard");
        } else {
            ESP_LOGW(TAG, "saved safety settings are from other firmware: menuconfig values until the dashboard sends new ones");
        }
    }
    nvs_close(h);
}

/* params == NULL: forget the saved values (back to menuconfig at the next boot too). */
static void params_save(const rb_params_t *params)
{
    nvs_handle_t h;
    if (!open_nvs(NVS_READWRITE, &h)) {
        ESP_LOGW(TAG, "NVS not available: the safety settings last until the next reboot");
        return;
    }
    esp_err_t err;
    if (params == NULL) {
        err = nvs_erase_key(h, NVS_KEY_PARAMS);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        const saved_params_t saved = {.version = 1, .layout = rb_params_layout_hash(), .params = *params};
        err = nvs_set_blob(h, NVS_KEY_PARAMS, &saved, sizeof(saved));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "safety settings not saved (%s): they last until the next reboot", esp_err_to_name(err));
    }
}

/* MQTT task: the dashboard's other safety settings, or back to menuconfig. */
static void set_params(const rb_ctrl_cmd_t *cmd)
{
    if (cmd->action == RB_CTRL_ACTION_PARAMS_DEFAULTS) {
        ESP_LOGW(TAG, "other safety settings: menuconfig values (request %u)", (unsigned)cmd->request_id);
        rb_controller_set_params(NULL);
        params_save(NULL);
        return;
    }
    ESP_LOGW(TAG, "other safety settings from the dashboard (request %u)", (unsigned)cmd->request_id);
    rb_controller_set_params(&cmd->params); /* the safety task refuses inconsistent values and keeps the ones in use */
    params_save(&cmd->params);
}
#endif

/* Safety task: the settings of rb_params.def that live outside it. */
static void params_applied(const rb_params_target_t *target, void *ctx)
{
    rb_outputs_set_verify_period_ms(target->verify_period_ms);
#if CONFIG_RB_CTRL_VALVE_WIRELESS
    rb_valve_link_set_keepalive_ms(target->valve_keepalive_ms);
#endif
}

#if CONFIG_RB_CTRL_REMOTE_COOKING_TEMP && !CONFIG_RB_SIM_NODE
/* The thermal node's MAC, learned from its packets, so the cooking temperature can reach it. */
static portMUX_TYPE s_thermal_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_thermal_node_id;
static bool s_thermal_has_mac;
static uint8_t s_thermal_mac[6];

static void learn_thermal_mac(const rb_espnow_rx_t *rx)
{
    static const uint8_t no_mac[6] = {0};
    if (rx->packet.role != RB_NODE_ROLE_THERMAL || rx->packet.node_id != s_thermal_node_id ||
        memcmp(rx->src_mac, no_mac, 6) == 0) {
        return;
    }
    portENTER_CRITICAL(&s_thermal_lock);
    memcpy(s_thermal_mac, rx->src_mac, 6);
    s_thermal_has_mac = true;
    portEXIT_CRITICAL(&s_thermal_lock);
}

/*
 * MQTT task: the cooking temperature also becomes the thermal node's hot-pixel
 * threshold, so the counted and outlined pixels are the ones that count as
 * cooking. Display only: the safety logic never depends on this arriving.
 */
static void send_thermal_threshold(uint16_t request_id, int16_t dc)
{
    uint8_t mac[6];
    portENTER_CRITICAL(&s_thermal_lock);
    const bool known = s_thermal_has_mac;
    memcpy(mac, s_thermal_mac, 6);
    portEXIT_CRITICAL(&s_thermal_lock);
    if (!known) {
        ESP_LOGW(TAG, "hot-pixel threshold not sent: nothing heard from the thermal node since boot");
        return;
    }
    rb_packet_t pkt = {.type = RB_MSG_THERMAL_CONFIG, .role = RB_NODE_ROLE_CONTROLLER, .uptime_ms = rb_time_mono_ms()};
    pkt.body.thermal_config = (rb_thermal_config_msg_t){
        .target_node_id = s_thermal_node_id,
        .request_id = request_id,
        .action = RB_THERMAL_ACTION_SET,
        .hot_threshold_centi = (int16_t)(dc * 10),
    };
    esp_err_t err = rb_espnow_add_peer(mac);
    if (err == ESP_OK) {
        err = rb_espnow_send(mac, &pkt);
    }
    ESP_LOGI(TAG, "hot-pixel threshold %.1f C to the thermal node: %s", dc / 10.0f,
             err == ESP_OK ? "sent" : esp_err_to_name(err));
}
#endif

#if CONFIG_RB_CTRL_REMOTE_COOKING_TEMP
/* MQTT task: the dashboard's cooking temperature, or back to menuconfig. */
static void set_cooking_temp(const rb_ctrl_cmd_t *cmd)
{
    const bool set = cmd->action == RB_CTRL_ACTION_COOKING_TEMP;
    const int16_t dc = set ? (int16_t)cmd->cooking_temp_dc : CONFIG_RB_SAFETY_HEAT_ON_DC;
    ESP_LOGW(TAG, "cooking temperature %.1f C (%s) from the dashboard (request %u)", dc / 10.0f,
             set ? "set" : "menuconfig default", (unsigned)cmd->request_id);
    rb_controller_set_cooking_temp(set ? dc / 10.0f : NAN);
    cooking_temp_save(!set, dc);
#if !CONFIG_RB_SIM_NODE
    send_thermal_threshold(cmd->request_id, dc);
#endif
}
#endif

static void node_packet(const rb_espnow_rx_t *rx, void *ctx)
{
    rb_c4002_relay_on_packet(rx);
#if CONFIG_RB_CTRL_REMOTE_COOKING_TEMP && !CONFIG_RB_SIM_NODE
    learn_thermal_mac(rx);
#endif
}
#if CONFIG_RB_CTRL_VALVE_WIRELESS
static bool valve_packet(const rb_packet_t *packet, uint32_t rx_ms, void *ctx)
{
    return rb_valve_link_on_packet(packet, rx_ms);
}
#endif

#if CONFIG_RB_CTRL_REMOTE_RESET || CONFIG_RB_CTRL_REMOTE_PRESENCE_FILTER || CONFIG_RB_CTRL_REMOTE_COOKING_TEMP || \
    CONFIG_RB_CTRL_REMOTE_COOKWARE || CONFIG_RB_CTRL_REMOTE_TIERS || CONFIG_RB_CTRL_REMOTE_ATTENDANCE || \
    CONFIG_RB_CTRL_REMOTE_RADAR_RULES || CONFIG_RB_CTRL_REMOTE_PARAMS || CONFIG_RB_CTRL_REMOTE_LOG
#define RB_CTRL_COMMANDS 1
/* MQTT task: operator reset, presence filter and cooking temperature sent from the dashboard (docs/remote_reset.md). */
static void on_controller_command(const char *topic, const char *data, size_t len, void *ctx)
{
    rb_ctrl_cmd_t cmd;
    char err[80];
    if (!rb_ctrl_cmd_parse(data, len, &cmd, err, sizeof(err))) {
        ESP_LOGW(TAG, "controller command rejected: %s", err);
        return;
    }
    if (strcmp(cmd.controller_id, CONFIG_RB_MQTT_CONTROLLER_ID) != 0) {
        ESP_LOGI(TAG, "command for controller %s ignored", cmd.controller_id);
        return;
    }
    switch (cmd.action) {
    case RB_CTRL_ACTION_RESET:
#if CONFIG_RB_CTRL_REMOTE_RESET
        ESP_LOGW(TAG, "operator reset from the dashboard (request %u)", (unsigned)cmd.request_id);
        rb_controller_request_reset();
#else
        ESP_LOGW(TAG, "dashboard reset refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_PRESENCE_FILTER:
    case RB_CTRL_ACTION_PRESENCE_FILTER_DEFAULTS:
#if CONFIG_RB_CTRL_REMOTE_PRESENCE_FILTER
        ESP_LOGW(TAG, "presence filter from the dashboard (request %u)", (unsigned)cmd.request_id);
        if (cmd.action == RB_CTRL_ACTION_PRESENCE_FILTER) {
            const rb_presence_filter_t filter = {
                .absence_debounce_ms = cmd.absence_ms,
                .presence_return_debounce_ms = cmd.return_ms,
                .presence_return_gap_ms = cmd.return_gap_ms,
                .warning_timeout_ms = cmd.warning_ms,
                .shutdown_timeout_ms = cmd.shutdown_ms,
            };
            rb_controller_set_presence_filter(&filter);
        } else {
            rb_controller_set_presence_filter(NULL);
        }
#else
        ESP_LOGW(TAG, "dashboard presence filter refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_COOKING_TEMP:
    case RB_CTRL_ACTION_COOKING_TEMP_DEFAULTS:
#if CONFIG_RB_CTRL_REMOTE_COOKING_TEMP
        set_cooking_temp(&cmd);
#else
        ESP_LOGW(TAG, "dashboard cooking temperature refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_COOKWARE:
    case RB_CTRL_ACTION_COOKWARE_DEFAULTS:
#if CONFIG_RB_CTRL_REMOTE_COOKWARE
        set_cookware(&cmd);
#else
        ESP_LOGW(TAG, "dashboard cookware profile refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_TIERS:
    case RB_CTRL_ACTION_TIERS_DEFAULTS:
#if CONFIG_RB_CTRL_REMOTE_TIERS
        set_tiers(&cmd);
#else
        ESP_LOGW(TAG, "dashboard temperature tiers refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_ATTENDANCE:
    case RB_CTRL_ACTION_ATTENDANCE_DEFAULTS:
#if CONFIG_RB_CTRL_REMOTE_ATTENDANCE
        set_attendance(&cmd);
#else
        ESP_LOGW(TAG, "dashboard attendance settings refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_RADAR_RULES:
    case RB_CTRL_ACTION_RADAR_RULES_DEFAULTS:
#if CONFIG_RB_CTRL_REMOTE_RADAR_RULES
        set_radar_rules(&cmd);
#else
        ESP_LOGW(TAG, "dashboard radar rules refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_PARAMS:
    case RB_CTRL_ACTION_PARAMS_DEFAULTS:
#if CONFIG_RB_CTRL_REMOTE_PARAMS
        set_params(&cmd);
#else
        ESP_LOGW(TAG, "dashboard safety settings refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_LOG_LEVEL:
#if CONFIG_RB_CTRL_REMOTE_LOG
        rb_log_set_level(cmd.log_tag, (esp_log_level_t)cmd.log_level);
        ESP_LOGW(TAG, "log level %s -> %s from the dashboard (until reboot)", cmd.log_tag,
                 rb_log_level_name((esp_log_level_t)cmd.log_level));
#else
        ESP_LOGW(TAG, "dashboard log level refused: turned off in menuconfig");
#endif
        break;
    case RB_CTRL_ACTION_LOG_LEVEL_DEFAULTS:
#if CONFIG_RB_CTRL_REMOTE_LOG
        rb_log_reset_levels();
        ESP_LOGW(TAG, "log levels back to menuconfig from the dashboard");
#else
        ESP_LOGW(TAG, "dashboard log level refused: turned off in menuconfig");
#endif
        break;
    }
}

static esp_err_t remote_commands_start(void)
{
    char filter[96];
    snprintf(filter, sizeof(filter), "%s/%s", CONFIG_RB_MQTT_TOPIC_PREFIX, RB_TOPIC_CONTROLLER_COMMAND);
    ESP_RETURN_ON_ERROR(rb_mqtt_subscribe(filter, 1, on_controller_command, NULL), TAG, "subscribe");
    ESP_LOGI(TAG, "dashboard commands: listening on %s", filter);
    return ESP_OK;
}
#endif

esp_err_t rb_controller_app_start(void)
{
#if CONFIG_RB_MQTT_PUBLISH_LOG
    rb_log_capture_start(); /* first, so the boot lines reach the dashboard too */
#endif
    rb_log_init();
    ESP_LOGI(TAG, "RB4107 controller %s starting, boot %s", CONFIG_RB_MQTT_CONTROLLER_ID, rb_time_boot_id());

    const rb_controller_config_t cfg = rb_controller_config_from_kconfig();
#if CONFIG_RB_CTRL_REMOTE_COOKING_TEMP && !CONFIG_RB_SIM_NODE
    s_thermal_node_id = cfg.nodes.thermal_node_id;
#endif
    ESP_RETURN_ON_ERROR(rb_controller_init(&cfg), TAG, "controller queues");
    rb_app_faults_init(&cfg.nodes);
    if (rb_wallclock_init() != ESP_OK) {
        fault_raise(FAULT_RTC, 0); /* timestamps only: safety unaffected */
    }
    rb_outputs_init(); /* a relay failure becomes a SAFETY fault via the self-test / tick hooks */

    const rb_controller_hooks_t hooks = {
        .apply_outputs = apply_outputs,
        .tick = rb_app_faults_tick,
        .self_test = rb_app_self_test,
        .safety_fault_active = rb_app_faults_safety_active,
        .node_events = rb_app_faults_node_events,
        .node_packet = node_packet,
#if CONFIG_RB_CTRL_VALVE_WIRELESS
        .valve_packet = valve_packet,
#endif
        .params_applied = params_applied,
    };
#if CONFIG_RB_CTRL_REMOTE_COOKING_TEMP
    cooking_temp_load(); /* applied at the first safety step */
#endif
#if CONFIG_RB_CTRL_REMOTE_COOKWARE
    cookware_load();
#endif
#if CONFIG_RB_CTRL_REMOTE_TIERS
    tiers_load();
#endif
#if CONFIG_RB_CTRL_REMOTE_ATTENDANCE
    attendance_load();
#endif
#if CONFIG_RB_CTRL_REMOTE_RADAR_RULES
    radar_rules_load();
#endif
#if CONFIG_RB_CTRL_REMOTE_PARAMS
    params_load();
#endif
    ESP_RETURN_ON_ERROR(rb_controller_start(&cfg, &hooks), TAG, "safety task");
#if CONFIG_RB_DIAG_TEST_TIMERS_AT_BOOT
    const safety_config_t test = rb_diag_test_safety_config();
    rb_controller_set_safety_config(&test, true);
#endif

#if CONFIG_RB_SIM_NODE
    ESP_RETURN_ON_ERROR(rb_sim_start(cfg.nodes.presence_node_ids, cfg.nodes.presence_node_count,
                                     cfg.nodes.thermal_node_id, 500),
                        TAG, "simulated nodes");
#else
    ESP_RETURN_ON_ERROR(rb_espnow_start(CONFIG_RB_ESPNOW_CHANNEL), TAG, "ESP-NOW");
#if CONFIG_RB_MQTT_PUBLISH_HEATMAP
    rb_heatmap_start(cfg.nodes.thermal_node_id); /* display only: pieces never reach the safety task */
#endif
    ESP_RETURN_ON_ERROR(rb_espnow_start_receiver(rb_controller_rx_queue()), TAG, "ESP-NOW receiver");
#if CONFIG_RB_CTRL_VALVE_WIRELESS
    if (rb_valve_link_start() != ESP_OK) {
        ESP_LOGE(TAG, "wireless valve link not started: the valve node will stay closed");
    }
#endif
#endif

    /* Telemetry path: best effort, never fatal. */
    if (rb_c4002_relay_start(cfg.nodes.presence_node_ids, cfg.nodes.presence_node_count) != ESP_OK) {
        ESP_LOGE(TAG, "remote C4002 tuning not started; safety unaffected");
    }
#ifdef RB_CTRL_COMMANDS
    if (remote_commands_start() != ESP_OK) {
        ESP_LOGE(TAG, "dashboard commands not started; reset from the console or by power cycle");
    }
#endif
    if (rb_telemetry_start() != ESP_OK) {
        ESP_LOGE(TAG, "telemetry task not started; safety unaffected");
    }
    if (rb_connectivity_start() != ESP_OK) {
        ESP_LOGE(TAG, "network not started; safety continues without telemetry");
    }

#if CONFIG_RB_DIAG_CONSOLE
    rb_diag_start();
#endif
#if CONFIG_RB_DIAG_CONTINUITY_MONITOR
    rb_continuity_start();
#endif
    return ESP_OK;
}
