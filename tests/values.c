#include "baro.h"

static int compare(int a, int b) { return a - b; }

TEST("[values_pass] comparisons keep C semantics") {
    int a = 0, b = 0;
    CHECK_EQ(a++, b++);
    REQUIRE_EQ(a, 1, "evaluated once");
    REQUIRE_EQ(b, 1);
    CHECK_LT(-1, 0);
    CHECK_GT(0.5, 0);
    uint64_t large = UINT64_MAX;
    CHECK_NE(large, large - 1);
    char letter = 'a';
    CHECK_LE(letter, 'a');
    const char *text = "text";
    char array[] = "text";
    CHECK_NE(text, array);
    CHECK_EQ(array, array);
    int *null = NULL;
    CHECK_EQ(null, NULL);
    CHECK_EQ(null, 0);
    CHECK_NE(&a, null);
    int (*function)(int, int) = compare;
    CHECK_EQ(function, compare);
    if (a) CHECK_GE(a, 1); else CHECK_GE(a, 2);
}

TEST("[values_fail] operands are displayed") {
    int x = 3;
    CHECK_EQ(x + 1, 5);
    CHECK_LT(2.5, 1.5);
    unsigned count = 7;
    CHECK_GE(count, 9u, "described");
    CHECK_NE(-1L, -1L);
    uint64_t large = UINT64_MAX;
    CHECK_EQ(large, 1);
    char letter = 'a';
    CHECK_EQ(letter, 'b');
    float half = 0.5f;
    CHECK_GT(half, 1);
    long double wide = 1.5L;
    CHECK_EQ(wide, 2.5L);
    CHECK_EQ(x, count); // Mixed signs: the values are shown as C compared them.
    int *other = NULL;
    CHECK_EQ(&x, other);
    REQUIRE_LE(x, 2);
    fprintf(stderr, "UNREACHABLE\n");
}

#ifndef _MSC_VER
// Types MSVC's C mode does not have.
TEST("[values_extra_pass] variably modified operands are evaluated once") {
    int n = 3;
    int grid[4][3] = {{0}};
    int (*first)[n] = grid, (*second)[n] = grid;
    CHECK_EQ(first, second++);
    CHECK_EQ(second, first + 1);
    CHECK_NE(first++, second++);
    CHECK_EQ(first, grid + 1);
    CHECK_EQ(second, grid + 2);
}
TEST("[values_extra_fail] unformatted types show no values") {
    float _Complex z = 1;
    CHECK_EQ(z, 2);
}
#endif
