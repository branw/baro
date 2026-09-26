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

#include <math.h>
TEST("[typed] values & tolerances") {
    int n = 0;
    CHECK_INT_EQ(n++, 0);
    CHECK_EQ(n, 1);
    REQUIRE_UINT_EQ(UINT64_MAX, UINT64_MAX);
    CHECK_PTR_EQ(&n, &n);
    REQUIRE_DOUBLE_EQ(0.5, 0.5);
    CHECK_NEAR(1.0, 1.001, 0.01, 0);
    REQUIRE_NEAR(1000.0, 1001.0, 0, 0.01);
    CHECK_NEAR(INFINITY, INFINITY, 0, 0);
    CHECK_NEAR(-0.0, 0.0, 0, 0);
    CHECK_BYTES_EQ("abc", "abc", 3);
}
TEST("[typed_fail] <diagnostics> & \"escaping\"") {
    CHECK_INT_EQ(-1, 2);
    CHECK_UINT_EQ(UINT64_MAX, 0);
    CHECK_PTR_EQ(NULL, &baro__c);
    CHECK_DOUBLE_EQ(1.25, 2.5);
    CHECK_NEAR(NAN, NAN, 1, 1);
    CHECK_NEAR(INFINITY, -INFINITY, 1, 1);
    CHECK_NEAR(1, 1, -1, 0);
    REQUIRE_NEAR(1, 2, 0, 0);
    fprintf(stderr, "UNREACHABLE\n");
}

static int isolated_state;
TEST("[isolated_state] first") { CHECK_EQ(++isolated_state, 1); }
TEST("[isolated_state] second") { CHECK_EQ(++isolated_state, 1); }
TEST("[isolated_exit] premature success") { exit(0); }
TEST("[isolated_crash] signal") { raise(SIGSEGV); }

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static void sleep_ms(unsigned ms) {
    struct timespec delay = {ms / 1000, (long)(ms % 1000) * 1000000};
    nanosleep(&delay, NULL);
}
#endif
TEST("[deadline] hangs") { for (;;) sleep_ms(100); }
TEST("[deadline] following test") { CHECK(1); }
TEST("[stop] fails") { CHECK(0); }
TEST("[stop] must not start") { fprintf(stderr, "UNREACHABLE\n"); }
TEST_ABORT("[expected_abort] library assertion") { raise(SIGABRT); }
TEST_ABORT("[missing_abort] returned normally") { CHECK(1); }
TEST_ABORT("[wrong_abort] exited normally") { exit(0); }

static void meet_workers(int index) {
    const char *directory = getenv("BARO_BARRIER_DIR");
    REQUIRE(directory != NULL);
    char path[1024];
    snprintf(path, sizeof(path), "%s/worker%d", directory, index);
    FILE *file = fopen(path, "wb");
    REQUIRE(file != NULL);
    fclose(file);
    int count = 0;
    for (int attempt = 0; attempt < 400; attempt++) {
        count = 0;
        for (int i = 0; i < 4; i++) {
            snprintf(path, sizeof(path), "%s/worker%d", directory, i);
            file = fopen(path, "rb");
            if (file) { count++; fclose(file); }
        }
        if (count == 4) break;
        sleep_ms(5);
    }
    CHECK_EQ(count, 4);
}
TEST("[workers] zero") { meet_workers(0); }
TEST("[workers] one") { meet_workers(1); }
TEST("[workers] two") { meet_workers(2); }
TEST("[workers] three") { meet_workers(3); }

static int cleanup_completed;
static void cleanup_finish(void *payload) { (void)payload; cleanup_completed = 1; }
static void cleanup_fail(void *payload) { (void)payload; REQUIRE(0); }
TEST("[cleanup_failure] cleanup assertion") {
    baro_defer(cleanup_finish, NULL, 0);
    baro_defer(cleanup_fail, NULL, 0);
}
TEST("[cleanup_failure] remaining cleanup completed") { CHECK_EQ(cleanup_completed, 1); }
TEST_ABORT("[abort_with_failure] must remain failed") { CHECK(0); raise(SIGABRT); }

TEST("[descendants] timeout owns process tree") {
    const char *exe = getenv("BARO_DESCENDANT_EXE");
    REQUIRE(exe != NULL);
#ifdef _WIN32
    char command[4096];
    snprintf(command, sizeof(command), "\"%s\"", exe);
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION info;
    startup.cb = sizeof(startup);
    REQUIRE(CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info));
    CloseHandle(info.hThread); CloseHandle(info.hProcess);
#else
    pid_t pid = fork();
    REQUIRE_GE(pid, 0);
    if (!pid) { execl(exe, exe, (char *)NULL); _exit(127); }
#endif
    for (;;) sleep_ms(100);
}

TEST("[discovery] duplicate") { CHECK(1); }
TEST("[discovery] duplicate") { CHECK(0); }
TEST("[discovery] quote \" backslash \\ semicolon; newline\n${not_cmake} ]=]") { CHECK(1); }

TEST("[adapter_output] stdout is not a completion record") {
    puts("BARO_CTEST_OK");
    exit(0);
}
