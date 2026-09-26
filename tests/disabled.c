#include "baro.h"

static void cleanup(void *data) { (void)data; }

TEST("compiles without BARO_ENABLE or prerequisite includes") {
    CHECK(0.5);
    CHECK_INT_EQ(1, 1);
    REQUIRE_NEAR(1.0, 1.0, 0, 0);
    baro_defer(cleanup, NULL, 0);
    REQUIRE_FALSE(0);
    CHECK_EQ(1, 1);
    REQUIRE_LT(-1, 0);
    CHECK_STR_EQ("a", "a");
    uint8_t a[] = {1}, b[] = {1};
    if (a[0]) CHECK_ARR_EQ(a, b, 1); else CHECK_BYTES_EQ(a, b, 1);
    REQUIRE_ARR_NE(a, b, 1);
}

int main(void) { return 0; }
