/* Dashboard reset: the controller command parser. */
#include <string.h>
#include "rb_ctrl_cmd.h"
#include "unity.h"

static bool parse(const char *json, rb_ctrl_cmd_t *cmd, char *err)
{
    return rb_ctrl_cmd_parse(json, strlen(json), cmd, err, 80);
}

static void test_reset_is_parsed(void)
{
    rb_ctrl_cmd_t cmd;
    char err[80];
    TEST_ASSERT_TRUE_MESSAGE(parse(" {\"schema_version\":2, \"type\":\"controller_command\",\"controller_id\":"
                                   "\"controller_01\",\"request_id\":65535,\"action\":\"reset\"}\n",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_RESET, cmd.action);
    TEST_ASSERT_EQUAL(65535, cmd.request_id);
    TEST_ASSERT_EQUAL_STRING("controller_01", cmd.controller_id);
}

static void test_presence_filter_is_parsed(void)
{
    rb_ctrl_cmd_t cmd;
    char err[80];
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":9,\"absence_ms\":2000,\"return_ms\":3000,\"return_gap_ms\":1000,"
                                   "\"warning_ms\":20000,\"shutdown_ms\":3600000,\"action\":\"presence_filter\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_PRESENCE_FILTER, cmd.action);
    TEST_ASSERT_EQUAL_UINT32(2000, cmd.absence_ms);
    TEST_ASSERT_EQUAL_UINT32(3000, cmd.return_ms);
    TEST_ASSERT_EQUAL_UINT32(1000, cmd.return_gap_ms);
    TEST_ASSERT_EQUAL_UINT32(20000, cmd.warning_ms);
    TEST_ASSERT_EQUAL_UINT32(3600000, cmd.shutdown_ms);

    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":10,\"action\":\"presence_filter_defaults\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_PRESENCE_FILTER_DEFAULTS, cmd.action);
}

static void test_bad_presence_filters_are_rejected(void)
{
#define HEAD "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,"
    static const char *const bad[] = {
        /* a value missing, above its limit, negative or a string */
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":2000,\"return_ms\":3000}",
        HEAD "\"action\":\"presence_filter\",\"warning_ms\":20000,\"shutdown_ms\":40000,"
             "\"absence_ms\":10001,\"return_ms\":3000,\"return_gap_ms\":1000}",
        HEAD "\"action\":\"presence_filter\",\"warning_ms\":20000,\"shutdown_ms\":40000,"
             "\"absence_ms\":2000,\"return_ms\":10001,\"return_gap_ms\":1000}",
        HEAD "\"action\":\"presence_filter\",\"warning_ms\":20000,\"shutdown_ms\":40000,"
             "\"absence_ms\":2000,\"return_ms\":3000,\"return_gap_ms\":5001}",
        HEAD "\"action\":\"presence_filter\",\"warning_ms\":20000,\"shutdown_ms\":40000,"
             "\"absence_ms\":-1,\"return_ms\":3000,\"return_gap_ms\":1000}",
        HEAD "\"action\":\"presence_filter\",\"warning_ms\":20000,\"shutdown_ms\":40000,"
             "\"absence_ms\":\"2000\",\"return_ms\":3000,\"return_gap_ms\":1000}",
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":2000,\"return_ms\":3000,\"return_gap_ms\":1000}",
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":2000,\"return_ms\":3000,\"return_gap_ms\":1000,"
             "\"warning_ms\":999,\"shutdown_ms\":40000}",
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":2000,\"return_ms\":3000,\"return_gap_ms\":1000,"
             "\"warning_ms\":20000,\"shutdown_ms\":3600001}",
        /* filter values with another action */
        HEAD "\"action\":\"reset\",\"return_ms\":3000}",
        HEAD "\"action\":\"reset\",\"warning_ms\":20000}",
        HEAD "\"action\":\"presence_filter_defaults\",\"absence_ms\":2000,\"return_ms\":3000,\"return_gap_ms\":1000}",
    };
#undef HEAD
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rb_ctrl_cmd_t cmd;
        char err[80];
        TEST_ASSERT_FALSE_MESSAGE(parse(bad[i], &cmd, err), bad[i]);
        TEST_ASSERT_TRUE(err[0] != '\0');
    }
}

static void test_bad_commands_are_rejected(void)
{
    static const char *const bad[] = {
        "",
        "[]",
        "{}",
        /* missing controller_id */
        "{\"schema_version\":2,\"type\":\"controller_command\",\"request_id\":1,\"action\":\"reset\"}",
        /* wrong type, action, version, request_id */
        "{\"schema_version\":2,\"type\":\"c4002_command\",\"controller_id\":\"c\",\"request_id\":1,\"action\":\"reset\"}",
        "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,\"action\":\"off\"}",
        "{\"schema_version\":1,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,\"action\":\"reset\"}",
        "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":0,\"action\":\"reset\"}",
        "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1.5,\"action\":\"reset\"}",
        "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"\",\"request_id\":1,\"action\":\"reset\"}",
        /* unknown or duplicate key, trailing data, unterminated */
        "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,\"action\":\"reset\","
        "\"force\":1}",
        "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,\"action\":\"reset\","
        "\"action\":\"reset\"}",
        "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,\"action\":\"reset\"}x",
        "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,\"action\":\"reset\"",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rb_ctrl_cmd_t cmd;
        char err[80];
        TEST_ASSERT_FALSE_MESSAGE(parse(bad[i], &cmd, err), bad[i]);
        TEST_ASSERT_TRUE(err[0] != '\0');
    }
}

static void test_cooking_temp_is_parsed(void)
{
    rb_ctrl_cmd_t cmd;
    char err[80];
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":11,\"action\":\"cooking_temp\",\"cooking_temp_dc\":625}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_COOKING_TEMP, cmd.action);
    TEST_ASSERT_EQUAL_UINT16(625, cmd.cooking_temp_dc);

    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":12,\"action\":\"cooking_temp_defaults\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_COOKING_TEMP_DEFAULTS, cmd.action);
}

static void test_cookware_is_parsed(void)
{
    rb_ctrl_cmd_t cmd;
    char err[80];
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":13,\"action\":\"cookware\",\"cookware\":\"liquid\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_COOKWARE, cmd.action);
    TEST_ASSERT_EQUAL_STRING("liquid", cmd.cookware);
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":14,\"action\":\"cookware_defaults\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_COOKWARE_DEFAULTS, cmd.action);
}

static void test_bad_cookware_is_rejected(void)
{
#define CHEAD "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,"
    static const char *const bad[] = {
        CHEAD "\"action\":\"cookware\"}",                                      /* the profile is missing */
        CHEAD "\"action\":\"cookware\",\"cookware\":\"steel\"}",                /* unknown profile */
        CHEAD "\"action\":\"cookware\",\"cookware\":5}",                        /* not a string */
        CHEAD "\"action\":\"reset\",\"cookware\":\"light\"}",                  /* goes with its action only */
        CHEAD "\"action\":\"cookware_defaults\",\"cookware\":\"light\"}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rb_ctrl_cmd_t cmd;
        char err[80];
        TEST_ASSERT_FALSE_MESSAGE(parse(bad[i], &cmd, err), bad[i]);
        TEST_ASSERT_TRUE(err[0] != '\0');
    }
#undef CHEAD
}

#define TIERS_BODY "\"tier1_temp_dc\":1200,\"tier1_warning_ms\":60000,\"tier1_shutdown_ms\":150000," \
                   "\"tier2_temp_dc\":2000,\"tier2_rate_dc_per_min\":200,\"tier2_rate_min_temp_dc\":1500," \
                   "\"tier2_warning_ms\":30000,\"tier2_shutdown_ms\":120000"

static void test_tiers_are_parsed(void)
{
    rb_ctrl_cmd_t cmd;
    char err[80];
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":15,\"action\":\"tiers\"," TIERS_BODY "}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_TIERS, cmd.action);
    TEST_ASSERT_EQUAL_UINT16(1200, cmd.tier1_temp_dc);
    TEST_ASSERT_EQUAL_UINT32(60000, cmd.tier1_warning_ms);
    TEST_ASSERT_EQUAL_UINT32(150000, cmd.tier1_shutdown_ms);
    TEST_ASSERT_EQUAL_UINT16(2000, cmd.tier2_temp_dc);
    TEST_ASSERT_EQUAL_UINT16(200, cmd.tier2_rate_dc_per_min);
    TEST_ASSERT_EQUAL_UINT16(1500, cmd.tier2_rate_min_temp_dc);
    TEST_ASSERT_EQUAL_UINT32(30000, cmd.tier2_warning_ms);
    TEST_ASSERT_EQUAL_UINT32(120000, cmd.tier2_shutdown_ms);
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":16,\"action\":\"tiers_defaults\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_TIERS_DEFAULTS, cmd.action);
}

static void test_bad_tiers_are_rejected(void)
{
#define THEAD "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,"
    static const char *const bad[] = {
        THEAD "\"action\":\"tiers\"}",                                                  /* the values are missing */
        THEAD "\"action\":\"tiers\",\"tier1_temp_dc\":1200}",                          /* only one of eight */
        THEAD "\"action\":\"reset\"," TIERS_BODY "}",                                  /* goes with its action only */
        THEAD "\"action\":\"tiers_defaults\",\"tier1_temp_dc\":1200}",
        THEAD "\"action\":\"tiers\",\"tier1_temp_dc\":3001,\"tier1_warning_ms\":60000,\"tier1_shutdown_ms\":150000,"
            "\"tier2_temp_dc\":2000,\"tier2_rate_dc_per_min\":200,\"tier2_rate_min_temp_dc\":1500,"
            "\"tier2_warning_ms\":30000,\"tier2_shutdown_ms\":120000}",                 /* temperature over the limit */
        THEAD "\"action\":\"tiers\",\"tier1_temp_dc\":1200,\"tier1_warning_ms\":500,\"tier1_shutdown_ms\":150000,"
            "\"tier2_temp_dc\":2000,\"tier2_rate_dc_per_min\":200,\"tier2_rate_min_temp_dc\":1500,"
            "\"tier2_warning_ms\":30000,\"tier2_shutdown_ms\":120000}",                 /* under 1 s */
        THEAD "\"action\":\"tiers\",\"tier1_temp_dc\":\"1200\",\"tier1_warning_ms\":60000,\"tier1_shutdown_ms\":150000,"
            "\"tier2_temp_dc\":2000,\"tier2_rate_dc_per_min\":200,\"tier2_rate_min_temp_dc\":1500,"
            "\"tier2_warning_ms\":30000,\"tier2_shutdown_ms\":120000}",                 /* a string */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rb_ctrl_cmd_t cmd;
        char err[80];
        TEST_ASSERT_FALSE_MESSAGE(parse(bad[i], &cmd, err), bad[i]);
        TEST_ASSERT_TRUE(err[0] != '\0');
    }
#undef THEAD
}

static void test_attendance_is_parsed(void)
{
    rb_ctrl_cmd_t cmd;
    char err[80];
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":17,\"action\":\"attendance\",\"pause_budget_ms\":30000,"
                                   "\"pause_budget_pct\":25,\"presence_fusion\":\"all\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_ATTENDANCE, cmd.action);
    TEST_ASSERT_EQUAL_UINT32(30000, cmd.pause_budget_ms);
    TEST_ASSERT_EQUAL_UINT8(25, cmd.pause_budget_pct);
    TEST_ASSERT_TRUE(cmd.fusion_all);
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":18,\"action\":\"attendance\",\"pause_budget_ms\":0,"
                                   "\"pause_budget_pct\":0,\"presence_fusion\":\"any\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_FALSE(cmd.fusion_all);
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":19,\"action\":\"attendance_defaults\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_ATTENDANCE_DEFAULTS, cmd.action);
}

static void test_bad_attendance_is_rejected(void)
{
#define AHEAD "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,"
    static const char *const bad[] = {
        AHEAD "\"action\":\"attendance\"}",
        AHEAD "\"action\":\"attendance\",\"pause_budget_ms\":30000,\"pause_budget_pct\":25}",   /* fusion missing */
        AHEAD "\"action\":\"attendance\",\"pause_budget_ms\":30000,\"pause_budget_pct\":101,\"presence_fusion\":\"all\"}",
        AHEAD "\"action\":\"attendance\",\"pause_budget_ms\":600001,\"pause_budget_pct\":25,\"presence_fusion\":\"all\"}",
        AHEAD "\"action\":\"attendance\",\"pause_budget_ms\":30000,\"pause_budget_pct\":25,\"presence_fusion\":\"most\"}",
        AHEAD "\"action\":\"reset\",\"presence_fusion\":\"all\"}",
        AHEAD "\"action\":\"attendance_defaults\",\"pause_budget_ms\":0}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rb_ctrl_cmd_t cmd;
        char err[80];
        TEST_ASSERT_FALSE_MESSAGE(parse(bad[i], &cmd, err), bad[i]);
        TEST_ASSERT_TRUE(err[0] != '\0');
    }
#undef AHEAD
}

#define RHEAD "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":21,"
#define RVALUES "\"radar_quiet_gap_ms\":1500,\"radar_dwell_ms\":4000,\"radar_stationary_min_ms\":1500," \
                "\"radar_settle_window_ms\":4000,\"radar_settle_span_cm\":60,\"radar_zone_max_cm\":0"

static void test_radar_rules_are_parsed(void)
{
    rb_ctrl_cmd_t cmd;
    char err[80];
    TEST_ASSERT_TRUE_MESSAGE(parse(RHEAD "\"action\":\"radar_rules\",\"radar_mode\":\"enforce\"," RVALUES "}", &cmd, err), err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_RADAR_RULES, cmd.action);
    TEST_ASSERT_EQUAL_STRING("enforce", cmd.radar_mode);
    TEST_ASSERT_EQUAL_UINT32(1500, cmd.radar_quiet_gap_ms);
    TEST_ASSERT_EQUAL_UINT32(4000, cmd.radar_dwell_ms);
    TEST_ASSERT_EQUAL_UINT32(1500, cmd.radar_stationary_min_ms);
    TEST_ASSERT_EQUAL_UINT32(4000, cmd.radar_settle_window_ms);
    TEST_ASSERT_EQUAL_UINT16(60, cmd.radar_settle_span_cm);
    TEST_ASSERT_EQUAL_UINT16(0, cmd.radar_zone_max_cm);
    TEST_ASSERT_TRUE_MESSAGE(parse(RHEAD "\"action\":\"radar_rules_defaults\"}", &cmd, err), err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_RADAR_RULES_DEFAULTS, cmd.action);
}

static void test_bad_radar_rules_are_rejected(void)
{
    static const char *const bad[] = {
        RHEAD "\"action\":\"radar_rules\"}",
        RHEAD "\"action\":\"radar_rules\"," RVALUES "}",                                    /* mode missing */
        RHEAD "\"action\":\"radar_rules\",\"radar_mode\":\"loud\"," RVALUES "}",
        RHEAD "\"action\":\"radar_rules\",\"radar_mode\":\"observe\",\"radar_dwell_ms\":4000}",
        RHEAD "\"action\":\"radar_rules\",\"radar_mode\":\"observe\",\"radar_quiet_gap_ms\":10001,"
              "\"radar_dwell_ms\":4000,\"radar_stationary_min_ms\":1500,\"radar_settle_window_ms\":4000,"
              "\"radar_settle_span_cm\":60,\"radar_zone_max_cm\":0}",
        RHEAD "\"action\":\"radar_rules\",\"radar_mode\":\"observe\",\"radar_quiet_gap_ms\":1500,"
              "\"radar_dwell_ms\":4000,\"radar_stationary_min_ms\":1500,\"radar_settle_window_ms\":4000,"
              "\"radar_settle_span_cm\":1001,\"radar_zone_max_cm\":0}",
        RHEAD "\"action\":\"reset\",\"radar_mode\":\"off\"}",
        RHEAD "\"action\":\"radar_rules_defaults\",\"radar_dwell_ms\":1}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rb_ctrl_cmd_t cmd;
        char err[80];
        TEST_ASSERT_FALSE_MESSAGE(parse(bad[i], &cmd, err), bad[i]);
        TEST_ASSERT_TRUE(err[0] != '\0');
    }
}

static void test_bad_cooking_temps_are_rejected(void)
{
#define HEAD "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\",\"request_id\":1,"
    static const char *const bad[] = {
        /* missing, above the limit, negative, a fraction or a string */
        HEAD "\"action\":\"cooking_temp\"}",
        HEAD "\"action\":\"cooking_temp\",\"cooking_temp_dc\":3001}",
        HEAD "\"action\":\"cooking_temp\",\"cooking_temp_dc\":-10}",
        HEAD "\"action\":\"cooking_temp\",\"cooking_temp_dc\":50.5}",
        HEAD "\"action\":\"cooking_temp\",\"cooking_temp_dc\":\"500\"}",
        /* the threshold with another action */
        HEAD "\"action\":\"cooking_temp_defaults\",\"cooking_temp_dc\":500}",
        HEAD "\"action\":\"reset\",\"cooking_temp_dc\":500}",
    };
#undef HEAD
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rb_ctrl_cmd_t cmd;
        char err[80];
        TEST_ASSERT_FALSE_MESSAGE(parse(bad[i], &cmd, err), bad[i]);
        TEST_ASSERT_TRUE(err[0] != '\0');
    }
}

/* A params command with every setting of rb_params.def at one end of its range (max = false: min). */
static void params_json(char *out, size_t cap, bool max, int skip, const char *extra)
{
    size_t n = (size_t)snprintf(out, cap, "{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                          "\"request_id\":3,\"action\":\"params\"");
    for (int i = 0; i < RB_PARAM_COUNT; i++) {
        if (i != skip) {
            n += (size_t)snprintf(out + n, cap - n, ",\"%s\":%lu", RB_PARAMS[i].key,
                                  (unsigned long)(max ? RB_PARAMS[i].max : RB_PARAMS[i].min));
        }
    }
    snprintf(out + n, cap - n, "%s}", extra);
}

static void test_params_are_parsed(void)
{
    static char json[2048];
    rb_ctrl_cmd_t cmd;
    char err[80];
    for (int end = 0; end < 2; end++) {
        params_json(json, sizeof(json), end == 1, -1, "");
        TEST_ASSERT_TRUE_MESSAGE(parse(json, &cmd, err), err);
        TEST_ASSERT_EQUAL(RB_CTRL_ACTION_PARAMS, cmd.action);
        for (int i = 0; i < RB_PARAM_COUNT; i++) {
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(end ? RB_PARAMS[i].max : RB_PARAMS[i].min, cmd.params.v[i], RB_PARAMS[i].key);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                                   "\"request_id\":3,\"action\":\"params_defaults\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_PARAMS_DEFAULTS, cmd.action);
}

static void test_bad_params_are_rejected(void)
{
    static char json[2048];
    rb_ctrl_cmd_t cmd;
    char err[80];
    /* one setting missing */
    params_json(json, sizeof(json), false, RB_PARAM_trend_window_ms, "");
    TEST_ASSERT_FALSE(parse(json, &cmd, err));
    /* one twice */
    params_json(json, sizeof(json), false, -1, ",\"tier_hyst_dc\":20");
    TEST_ASSERT_FALSE(parse(json, &cmd, err));
    /* one out of range, below and above */
    params_json(json, sizeof(json), false, RB_PARAM_self_test_timeout_ms, ",\"self_test_timeout_ms\":999");
    TEST_ASSERT_FALSE(parse(json, &cmd, err));
    params_json(json, sizeof(json), true, RB_PARAM_valve_keepalive_ms, ",\"valve_keepalive_ms\":2501");
    TEST_ASSERT_FALSE(parse(json, &cmd, err));
    /* a string */
    params_json(json, sizeof(json), false, RB_PARAM_shutdown_timing, ",\"shutdown_timing\":\"1\"");
    TEST_ASSERT_FALSE(parse(json, &cmd, err));
    /* a setting with another action */
    TEST_ASSERT_FALSE(parse("{\"schema_version\":2,\"type\":\"controller_command\",\"controller_id\":\"c\","
                            "\"request_id\":3,\"action\":\"params_defaults\",\"tier_hyst_dc\":20}",
                            &cmd, err));
    TEST_ASSERT_TRUE(err[0] != '\0');
}

void run_ctrl_cmd_tests(void)
{
    RUN_TEST(test_reset_is_parsed);
    RUN_TEST(test_bad_commands_are_rejected);
    RUN_TEST(test_presence_filter_is_parsed);
    RUN_TEST(test_bad_presence_filters_are_rejected);
    RUN_TEST(test_cooking_temp_is_parsed);
    RUN_TEST(test_bad_cooking_temps_are_rejected);
    RUN_TEST(test_cookware_is_parsed);
    RUN_TEST(test_bad_cookware_is_rejected);
    RUN_TEST(test_tiers_are_parsed);
    RUN_TEST(test_bad_tiers_are_rejected);
    RUN_TEST(test_attendance_is_parsed);
    RUN_TEST(test_bad_attendance_is_rejected);
    RUN_TEST(test_radar_rules_are_parsed);
    RUN_TEST(test_bad_radar_rules_are_rejected);
    RUN_TEST(test_params_are_parsed);
    RUN_TEST(test_bad_params_are_rejected);
}
