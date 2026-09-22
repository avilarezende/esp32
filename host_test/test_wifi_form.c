/*
 * Host unit tests for the wifi_form component (pure C, no hardware).
 *
 * Compiled and run natively with gcc via run.sh, reusing the real
 * components/wifi_form/wifi_form.c and the Unity framework bundled with
 * ESP-IDF.
 */

#include <string.h>

#include "unity.h"
#include "wifi_form.h"

void setUp(void) {}
void tearDown(void) {}

static void test_url_decode_basic(void)
{
    char out[64];
    wifi_form_url_decode("a+b", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a b", out);

    wifi_form_url_decode("MyHome%2FWiFi", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("MyHome/WiFi", out);

    wifi_form_url_decode("p%40ss%20word%21", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("p@ss word!", out);

    wifi_form_url_decode("plain", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("plain", out);
}

static void test_url_decode_edge(void)
{
    char out[8];
    /* Truncation must stay within bounds and remain NUL-terminated. */
    wifi_form_url_decode("abcdefghijklmnop", out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(7, strlen(out));

    /* A stray '%' without two hex digits is passed through literally. */
    char out2[16];
    wifi_form_url_decode("100%", out2, sizeof(out2));
    TEST_ASSERT_EQUAL_STRING("100%", out2);
}

static void test_get_field(void)
{
    char v[64];

    TEST_ASSERT_TRUE(wifi_form_get_field("ssid=Home&password=secret", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("Home", v);

    TEST_ASSERT_TRUE(wifi_form_get_field("ssid=Home&password=secret", "password", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("secret", v);

    /* Values are URL-decoded. */
    TEST_ASSERT_TRUE(wifi_form_get_field("ssid=My+Net%21&password=a%2Fb", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("My Net!", v);
    TEST_ASSERT_TRUE(wifi_form_get_field("ssid=My+Net%21&password=a%2Fb", "password", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("a/b", v);

    /* Missing key. */
    TEST_ASSERT_FALSE(wifi_form_get_field("ssid=Home", "password", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);

    /* A key must match a whole token, not be a prefix of another key. */
    TEST_ASSERT_FALSE(wifi_form_get_field("ssidx=Home", "ssid", v, sizeof(v)));
}

static void test_valid_ssid(void)
{
    char ssid32[33];
    memset(ssid32, 'a', 32);
    ssid32[32] = '\0';
    char ssid33[34];
    memset(ssid33, 'a', 33);
    ssid33[33] = '\0';

    TEST_ASSERT_FALSE(wifi_form_valid_ssid(""));
    TEST_ASSERT_TRUE(wifi_form_valid_ssid("A"));
    TEST_ASSERT_TRUE(wifi_form_valid_ssid("MyHomeWiFi"));
    TEST_ASSERT_TRUE(wifi_form_valid_ssid(ssid32));
    TEST_ASSERT_FALSE(wifi_form_valid_ssid(ssid33));
    TEST_ASSERT_FALSE(wifi_form_valid_ssid(NULL));
}

static void test_valid_password(void)
{
    char pass63[64];
    memset(pass63, 'x', 63);
    pass63[63] = '\0';
    char pass64[65];
    memset(pass64, 'x', 64);
    pass64[64] = '\0';

    TEST_ASSERT_TRUE(wifi_form_valid_password(""));         /* open network */
    TEST_ASSERT_FALSE(wifi_form_valid_password("1234567")); /* 7 chars */
    TEST_ASSERT_TRUE(wifi_form_valid_password("12345678")); /* 8 chars */
    TEST_ASSERT_TRUE(wifi_form_valid_password(pass63));
    TEST_ASSERT_FALSE(wifi_form_valid_password(pass64));
    TEST_ASSERT_FALSE(wifi_form_valid_password(NULL));
}

static void test_json_escape(void)
{
    char out[64];
    wifi_form_json_escape("plain", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("plain", out);

    wifi_form_json_escape("a\"b\\c", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c", out);

    wifi_form_json_escape("line1\nline2\t!", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("line1\\nline2\\t!", out);

    /* Control char below 0x20 becomes \u00XX. */
    char in[2] = { 0x01, 0x00 };
    wifi_form_json_escape(in, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("\\u0001", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_url_decode_basic);
    RUN_TEST(test_url_decode_edge);
    RUN_TEST(test_get_field);
    RUN_TEST(test_valid_ssid);
    RUN_TEST(test_valid_password);
    RUN_TEST(test_json_escape);
    return UNITY_END();
}
