/*
 * Hysteresis of the internet globe. The DNS/HTTP probe itself needs lwIP;
 * this checks the rule the probe task applies to each round.
 */

#include "cyd_net_logic.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_period_is_inside_the_requested_window(void)
{
    TEST_ASSERT_TRUE(CYD_NET_PERIOD_MS >= 30000);
    TEST_ASSERT_TRUE(CYD_NET_PERIOD_MS <= 60000);
    TEST_ASSERT_EQUAL_INT(2, CYD_NET_FAILS_OFFLINE);
    TEST_ASSERT_TRUE(CYD_NET_DNS_MS <= 3000);
    TEST_ASSERT_TRUE(CYD_NET_HTTP_MS <= 3000);
}

static void test_one_success_is_online_and_two_failures_are_offline(void)
{
    int fails = 0;
    int online = 0;

    online = cyd_net_apply(online, &fails, 1, 1);
    TEST_ASSERT_EQUAL_INT(1, online);
    TEST_ASSERT_EQUAL_INT(0, fails);

    online = cyd_net_apply(online, &fails, 1, 0);
    TEST_ASSERT_EQUAL_INT(1, online);
    TEST_ASSERT_EQUAL_INT(1, fails);

    online = cyd_net_apply(online, &fails, 1, 0);
    TEST_ASSERT_EQUAL_INT(0, online);
    TEST_ASSERT_EQUAL_INT(2, fails);

    online = cyd_net_apply(online, &fails, 1, 1);
    TEST_ASSERT_EQUAL_INT(1, online);
    TEST_ASSERT_EQUAL_INT(0, fails);
}

static void test_link_down_is_offline_without_counting_a_failure(void)
{
    int fails = 7;
    int online = cyd_net_apply(1, &fails, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, online);
    TEST_ASSERT_EQUAL_INT(0, fails);

    /* First failed round after the link returns does not flip a fresh
     * offline state, and does not inherit the old streak. */
    online = cyd_net_apply(online, &fails, 1, 0);
    TEST_ASSERT_EQUAL_INT(0, online);
    TEST_ASSERT_EQUAL_INT(1, fails);
}

static void test_starts_offline_until_a_success(void)
{
    int fails = 0;
    int online = cyd_net_apply(0, &fails, 1, 0);
    TEST_ASSERT_EQUAL_INT(0, online);
    TEST_ASSERT_EQUAL_INT(1, fails);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_period_is_inside_the_requested_window);
    RUN_TEST(test_one_success_is_online_and_two_failures_are_offline);
    RUN_TEST(test_link_down_is_offline_without_counting_a_failure);
    RUN_TEST(test_starts_offline_until_a_success);
    return UNITY_END();
}
