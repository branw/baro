#include <baro.h>
#include <stdlib.h>
#include <string.h>

TEST("check fails soft") {
    CHECK(1);
    CHECK(0); // should fail

    CHECK_FALSE(0);
    CHECK_FALSE(1); // should fail

    CHECK_EQ(0, 0);
    CHECK_EQ(0, 1); // should fail
    CHECK_EQ(1, 0); // should fail
    CHECK_EQ(1, 1);

    CHECK_NE(0, 0); // should fail
    CHECK_NE(0, 1);
    CHECK_NE(1, 0);
    CHECK_NE(1, 1); // should fail

    CHECK_LT(0, 0); // should fail
    CHECK_LT(0, 1);
    CHECK_LT(1, 0); // should fail
    CHECK_LT(1, 1); // should fail

    CHECK_LE(0, 0);
    CHECK_LE(0, 1);
    CHECK_LE(1, 0); // should fail
    CHECK_LE(1, 1);

    CHECK_GE(0, 0);
    CHECK_GE(0, 1); // should fail
    CHECK_GE(1, 0);
    CHECK_GE(1, 1);

    CHECK_GT(0, 0); // should fail
    CHECK_GT(0, 1); // should fail
    CHECK_GT(1, 0);
    CHECK_GT(1, 1); // should fail
}

TEST("require fails hard") {
    REQUIRE(1);

    REQUIRE(0); // should fail

    REQUIRE(0);
}

TEST("string comparisons") {
    char const *foo = "foo";
    CHECK_STR_EQ("foo", foo);
    CHECK_STR_NE("foo", foo); // should fail

#ifdef _WIN32
    char *foo_dup = _strdup(foo);
#else
    char *foo_dup = strdup(foo);
#endif
    CHECK_STR_EQ(foo, foo_dup);
    CHECK_STR_EQ("foo", foo_dup);

    CHECK_STR_ICASE_EQ("Foo", foo);
    CHECK_STR_ICASE_EQ("FOO", foo_dup);
    CHECK_STR_ICASE_EQ("BAR", foo_dup); // should fail

    free(foo_dup);
}

TEST("fail with message") {
    CHECK(0, "displays message on check failure");
    CHECK(1, "doesn't display message on check pass");

    REQUIRE(1, "doesn't display message on require pass");
    REQUIRE(0, "displays message on require failure");
}

TEST("string comparisons with messages") {
    CHECK_STR_EQ("foo", "bar", "message");
    CHECK_STR_NE("foo", "foo", "message");
    CHECK_STR_ICASE_EQ("foo", "bar", "message");
    CHECK_STR_ICASE_NE("foo", "foo", "message");
}

TEST("array comparisons") {
    uint8_t a[] = { 1, 2, 0, 4 };
    uint8_t b[] = { 1, 2, 3, 4 };

    CHECK_ARR_NE(a, b, 4);
    CHECK_ARR_EQ(a, b, 4); // should fail
    CHECK_ARR_NE(b, a, 4);

    CHECK_ARR_NE(a, b, 3);
    CHECK_ARR_EQ(a, b, 3); // should fail
    CHECK_ARR_NE(b, a, 3);

    CHECK_ARR_EQ(a, b, 2);
    CHECK_ARR_NE(a, b, 2); // should fail

    CHECK_ARR_EQ(a, b, 1);

    CHECK_ARR_EQ(a, b, 0);

    a[2] = 3;
    CHECK_ARR_EQ(a, b, 4);
    CHECK_ARR_NE(a, b, 4); // should fail

    uint32_t c[] = {0x1234, 0x5678};
    uint32_t d[] = {0x1234, 0x8765};

    CHECK_ARR_NE(c, d, 2);
    CHECK_ARR_EQ(d, c, 1);

    *(((uint8_t *)c) + 1) = 0x35;
    CHECK_ARR_NE(c, d, 1);
    CHECK_ARR_EQ((uint8_t *) c, (uint8_t *) d, 1);
}

TEST("array comparisons with messages") {
    uint64_t a[] = {1, 2, 3};
    uint64_t b[] = {1, 2, 4};

    CHECK_ARR_NE(a, b, 2, "eq");
    CHECK_ARR_EQ(a, b, 3, "not eq");
}