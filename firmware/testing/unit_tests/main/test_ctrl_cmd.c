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
                                   "\"action\":\"presence_filter\"}",
                                   &cmd, err),
                             err);
    TEST_ASSERT_EQUAL(RB_CTRL_ACTION_PRESENCE_FILTER, cmd.action);
    TEST_ASSERT_EQUAL_UINT32(2000, cmd.absence_ms);
    TEST_ASSERT_EQUAL_UINT32(3000, cmd.return_ms);
    TEST_ASSERT_EQUAL_UINT32(1000, cmd.return_gap_ms);

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
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":10001,\"return_ms\":3000,\"return_gap_ms\":1000}",
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":2000,\"return_ms\":10001,\"return_gap_ms\":1000}",
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":2000,\"return_ms\":3000,\"return_gap_ms\":5001}",
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":-1,\"return_ms\":3000,\"return_gap_ms\":1000}",
        HEAD "\"action\":\"presence_filter\",\"absence_ms\":\"2000\",\"return_ms\":3000,\"return_gap_ms\":1000}",
        /* filter values with another action */
        HEAD "\"action\":\"reset\",\"return_ms\":3000}",
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

void run_ctrl_cmd_tests(void)
{
    RUN_TEST(test_reset_is_parsed);
    RUN_TEST(test_bad_commands_are_rejected);
    RUN_TEST(test_presence_filter_is_parsed);
    RUN_TEST(test_bad_presence_filters_are_rejected);
}
