#include "baro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif
TEST("[duplicate] same name") { CHECK(1); }
TEST("[duplicate] same name") { CHECK(0); }
TEST("[pass][semi;tag] quote \" slash \\ semi; newline\nCRLF\r\n${not_cmake} ]=]") { CHECK(1); }
TEST("[pass][context] environment and directory") {
    CHECK_STR_EQ(getenv("BARO_VALUE"), "alpha;beta");
    CHECK_STR_EQ(getenv("BARO_DOLLAR"), "${literal}");
    CHECK_STR_EQ(getenv("BARO_CONFIG"), EXPECTED_CONFIG);
    FILE *file = fopen("token", "rb");
    REQUIRE(file != NULL);
    fclose(file);
}
TEST("[timeout] CTest owns the deadline") {
    for (;;) {
#ifdef _WIN32
        Sleep(100);
#else
        struct timespec delay = {0, 100000000};
        nanosleep(&delay, NULL);
#endif
    }
}
TEST("[crash] ordinary library assertion") { assert(0); }
TEST_ABORT("[pass] expected library assertion") { assert(0); }
TEST_ABORT("[expected_timeout] CTest owns expected-abort deadline") {
    for (;;) {
#ifdef _WIN32
        Sleep(100);
#else
        struct timespec delay = {0, 100000000};
        nanosleep(&delay, NULL);
#endif
    }
}

int main(int argc, char *argv[]) {
    /* Discovery requires the same directory/environment as test execution. */
    const char *value = getenv("BARO_VALUE");
    if (!value || strcmp(value, "alpha;beta")) return 20;
    FILE *file = fopen("token", "rb");
    if (!file) return 21;
    fclose(file);
    int status = baro_run(argc, argv);
    return getenv("BARO_MAIN_FAIL") ? 17 : status;
}

TEST_ABORT("[premature] exit zero is not an abort") { exit(0); }
TEST_ABORT("[premature] immediate exit is not an abort") { _exit(0); }
TEST_ABORT("[premature] assertion then abort remains failed") { CHECK(0); abort(); }
TEST_ABORT("[premature] normal return is not an abort") { CHECK(1); }

TEST("[completion] exit zero") { exit(0); }
TEST("[completion] immediate exit zero") { _exit(0); }
TEST("[completion] failed then exit zero") { CHECK(0); exit(0); }
TEST("[completion] failed then immediate exit zero") { CHECK(0); _exit(0); }
static void bad_cleanup(void *data) { (void)data; CHECK(0); }
TEST("[completion] cleanup failure") { baro_defer(bad_cleanup, NULL, 0); }
TEST("[descendant_timeout] CTest kills descendants") {
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
    for (;;) Sleep(100);
#else
    pid_t pid = fork();
    REQUIRE_GE(pid, 0);
    if (!pid) { execl(exe, exe, (char *)NULL); _exit(127); }
    for (;;) { struct timespec delay = {0, 100000000}; nanosleep(&delay, NULL); }
#endif
}
