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
}
