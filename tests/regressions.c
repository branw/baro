#include "baro.h"
#ifndef _WIN32
#include <sys/resource.h>
#endif

TEST("[numeric] original types and single evaluation") {
    CHECK(0.5);
    REQUIRE(-0.5);
    CHECK_FALSE(0.0);
    REQUIRE_FALSE(0.0);
    CHECK_LT(-1, 0);
    REQUIRE_LE(-1, 0);
    CHECK_GT(0, -1);
    REQUIRE_GE(0, -1);
    CHECK_NE(1.1, 1.9);
    REQUIRE_EQ(1.1, 1.1);
    uint64_t large = UINT64_MAX;
    CHECK_NE(large, large - 1);
    int a = 0, b = 0;
    CHECK_EQ(a++, b++);
    REQUIRE_EQ(a, 1);
    REQUIRE_EQ(b, 1);
    CHECK_EQ(&a, &a);
    CHECK_NE(&a, &b);
}

TEST("[float_failure] distinct fractions") { CHECK_EQ(1.1, 1.9); }

TEST("[require] string requirement stops execution") {
    REQUIRE_STR_EQ("a", "b");
    fprintf(stderr, "UNREACHABLE\n");
}

TEST("[output] preserve output policy after failure") {
    puts("BEFORE_FAILURE");
    CHECK(0);
    puts("AFTER_FAILURE");
}
TEST("[output] preserve output policy for next test") { puts("NEXT_TEST"); }

TEST("[descriptors] repeated capture and restoration") {
#ifndef _WIN32
    struct rlimit limit;
    REQUIRE_EQ(getrlimit(RLIMIT_NOFILE, &limit), 0);
    if (limit.rlim_cur > 64) limit.rlim_cur = 64;
    REQUIRE_EQ(setrlimit(RLIMIT_NOFILE, &limit), 0);
#endif
    for (int i = 0; i < 200; i++) {
        baro__redirect_output(&baro__c, 0);
        baro__redirect_output(&baro__c, 1);
        baro__redirect_output(&baro__c, 1); // Already capturing: do not duplicate again.
    }
    int fd = BARO__DUP(BARO__FILENO(stdout));
    REQUIRE_GE(fd, 0);
    BARO__CLOSE(fd);
    for (int i = 0; i < 100; i++) CHECK(0);
}

TEST("[suppression] hide successful output and stderr") {
    puts("HIDDEN_STDOUT");
    fprintf(stderr, "HIDDEN_STDERR\n");
}

static void failing_test(void) { CHECK(0); }
static struct baro__tag const many_tag = {"[many] failing test", __FILE__, __LINE__};
BARO__INITIALIZER(register_many) {
    for (int i = 0; i < 256; i++) baro__register_test(failing_test, &many_tag);
}

#include <signal.h>
TEST("[abort] first signal") { raise(SIGABRT); }
TEST("[abort] second signal") { raise(SIGABRT); }
TEST("[abort] runner continues") { CHECK(1); }

TEST("[null_strings] defined null comparisons") {
    CHECK_STR_EQ(NULL, NULL);
    REQUIRE_STR_ICASE_EQ(NULL, NULL);
    CHECK_STR_NE(NULL, "");
    REQUIRE_STR_NE("", NULL);
    CHECK_STR_ICASE_NE(NULL, "value");
    REQUIRE_STR_ICASE_NE("value", NULL);
    CHECK_STR_EQ("value", "value");
    CHECK_STR_ICASE_EQ("VALUE", "value");
}
TEST("[null_failures] null diagnostics") {
    CHECK_STR_EQ(NULL, "value");
    CHECK_STR_EQ("value", NULL);
    CHECK_STR_ICASE_NE(NULL, NULL);
    REQUIRE_STR_ICASE_EQ(NULL, "value");
    fprintf(stderr, "UNREACHABLE\n");
}

static void capture_bytes(size_t count) {
    for (size_t i = 0; i < count; i++) {
        putchar('A' + (int)(i % 26));
        if (i == count / 2) fflush(stdout);
    }
    fflush(stdout);
    CHECK(0);
    printf("SECOND_SEGMENT");
    fflush(stdout);
    CHECK(0);
}
TEST("[capture4095] below limit") { capture_bytes(4095); }
TEST("[capture4096] at limit") { capture_bytes(4096); }
TEST("[capture4097] above limit") { capture_bytes(4097); }
TEST("[capture9000] multiple buffers") { capture_bytes(9000); }
TEST("[capture_isolation] successful output") { puts("PASS_ONLY"); }
TEST("[capture_isolation] failed output") { puts("FAIL_ONLY"); CHECK(0); }

static int cleanup_order;
static void cleanup_record(void *data) {
    int value;
    memcpy(&value, data, sizeof(value));
    cleanup_order = cleanup_order * 10 + value;
}
TEST("[cleanup] unwind copied payloads") {
    int value = 1;
    baro_defer(cleanup_record, &value, sizeof(value));
    value = 2;
    baro_defer(cleanup_record, &value, sizeof(value));
    value = 99;
    REQUIRE(0);
}
TEST("[cleanup] callbacks completed") {
    CHECK_EQ(cleanup_order, 21);
    int value = 3;
    baro_defer(cleanup_record, &value, sizeof(value));
    CHECK(1);
}
