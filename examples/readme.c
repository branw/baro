#include "baro.h"

static int add(int a, int b) { return a + b; }

TEST("[math] addition") {
    CHECK_INT_EQ(add(2, 3), 5);
    REQUIRE(add(-1, 1) == 0);
}
