#include "baro.h"

TEST("compiles without BARO_ENABLE or prerequisite includes") {
    CHECK(0.5);
    REQUIRE_FALSE(0);
    CHECK_EQ(1, 1);
    REQUIRE_LT(-1, 0);
    CHECK_STR_EQ("a", "a");
    uint8_t a[] = {1}, b[] = {1};
    CHECK_ARR_EQ(a, b, 1);
    REQUIRE_ARR_NE(a, b, 1);
}

int main(void) { return 0; }
