#include <signal.h>
#include "baro.h"

struct baro__context baro__c = {0};

#include <errno.h>
#include <inttypes.h>
#include <math.h>

static size_t baro__positive(const char *text) {
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || !*text || *end || text[0] == '-' || !value || value > SIZE_MAX) {
        fprintf(stderr, "Expected a positive integer: %s\n", text);
        return 0;
    }
    return (size_t)value;
}

struct baro__result {
    int ran, failed;
    size_t asserts, asserts_failed;
    const char *reason;
};

static void baro__xml(FILE *out, const char *text) {
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        switch (*p) {
        case '&': fputs("&amp;", out); break;
        case '<': fputs("&lt;", out); break;
        case '>': fputs("&gt;", out); break;
        case '\"': fputs("&quot;", out); break;
        case '\'': fputs("&apos;", out); break;
        default: if (*p >= 32 || *p == '\n' || *p == '\t') fputc(*p, out);
        }
    }
}

static int baro__junit(const char *path, struct baro__test_list *tests,
                       struct baro__result *results) {
    FILE *out = fopen(path, "wb");
    if (!out) { perror("Cannot write JUnit report"); return 0; }
    fprintf(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<testsuite name=\"baro\" tests=\"%zu\" failures=\"%zu\">\n",
                 baro__c.num_tests_ran, baro__c.num_tests_failed);
    for (size_t i = 0; i < tests->size; i++) {
        if (!results[i].ran) continue;
        fputs("  <testcase name=\"", out);
        baro__xml(out, tests->tests[i].tag->desc);
        fputs("\" file=\"", out);
        baro__xml(out, tests->tests[i].tag->file_path);
        fprintf(out, "\" line=\"%d\" assertions=\"%zu\">", tests->tests[i].tag->line_num, results[i].asserts);
        if (results[i].failed) {
            fputs("<failure message=\"", out);
            baro__xml(out, results[i].reason ? results[i].reason : "Assertion failure");
            fprintf(out, "\">%zu failed assertions</failure>", results[i].asserts_failed);
        }
        fputs("</testcase>\n", out);
    }
    fputs("</testsuite>\n", out);
    int ok = !ferror(out);
    if (fclose(out)) ok = 0;
    return ok;
}

static void set_sigabrt_handler(void (*handler)(int)) {
#ifdef _WIN32
    signal(SIGABRT, handler);
#else
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = handler;
    sigaction(SIGABRT, &action, NULL);
#endif
}

static void handle_signal(int signum) {
    if (signum == SIGABRT) {
        set_sigabrt_handler(NULL);

        // Return to the main loop because we can't do anything useful while
        // still in the signal handler
        BARO__LONGJMP(baro__c.env, BARO__JMP_SIGABRT);
    }
}

int baro_run(
        int argc,
        char *argv[]) {
    int show_passed_tests = 0;
    int suppress_stdout = 1;
    int suppress_stderr = 0;
    int stop_after_failure = 0;
    int recover_abort = 0;
    size_t num_partitions = 1;
    size_t cur_partition = 1;
    char *raw_tag_filters = NULL;
    const char *exact_name = NULL, *junit_path = NULL;
    int list_tests = 0, allow_empty = 0;
    char *baro__optarg = NULL;

    size_t const total_num_tests = baro__c.tests.size;

    // Parse short clusters and long options without exporting getopt symbols.
    for (int arg = 1; arg < argc; arg++) {
        const char *option = argv[arg];
        if (strncmp(option, "--", 2) == 0) {
            if (!strcmp(option, "--list-tests")) { list_tests = 1; continue; }
            if (!strcmp(option, "--allow-empty")) { allow_empty = 1; continue; }
            if (!strcmp(option, "--recover-abort")) { recover_abort = 1; continue; }
            if (!strcmp(option, "--test") || !strcmp(option, "--junit")) {
                if (++arg == argc) { fprintf(stderr, "Missing value for %s\n", option); return EXIT_FAILURE; }
                if (!strcmp(option, "--test")) exact_name = argv[arg];
                else junit_path = argv[arg];
                continue;
            }
            fprintf(stderr, "Unknown option: %s\n", option);
            return EXIT_FAILURE;
        }
        if (*option++ != '-' || !*option) { fprintf(stderr, "Unexpected argument\n"); return EXIT_FAILURE; }
        while (*option) {
            int c = *option++;
            if (c == 'p' || c == 'n' || c == 't') {
                if (*option) { baro__optarg = (char *)option; option += strlen(option); }
                else if (++arg < argc) baro__optarg = argv[arg];
                else { fprintf(stderr, "Missing option value\n"); return EXIT_FAILURE; }
            }
        switch (c) {
        case 'r': recover_abort = 1; break;
        case 'p':
            num_partitions = baro__positive(baro__optarg);
            if (!num_partitions) return EXIT_FAILURE;
            break;

        case 'n':
            cur_partition = baro__positive(baro__optarg);
            if (!cur_partition) return EXIT_FAILURE;
            break;

        case 'a':
            show_passed_tests = 1;
            break;

        case 'o':
            suppress_stdout = 0;
            break;
            
        case 'e':
            suppress_stderr = 1;
            break;

        case 's':
            stop_after_failure = 1;
            break;

        case 't':
            free(raw_tag_filters);
#ifdef _WIN32
            raw_tag_filters = _strdup(baro__optarg);
#else
            raw_tag_filters = strdup(baro__optarg);
#endif
            break;

        case 'h':
            printf("Unit test suite, powered by baro; %zu tests loaded\n"
                   "Usage: %s [options]\n"
                   "Options:\n"
                   "  -a                   Show all tests, even passing ones\n"
                   "  -o                   Show all standard output (stdout), including passed tests\n"
                   "  -e                   Hide standard error (stderr) output\n"
                   "  -s                   Stop running after the first failure\n"
                   "  -t <tag1,tag2,...>   Only run tests with one of these [tags]\n"
                   "  -p <num_partitions>  Total number of partitions, 1-based\n"
                   "  -n <cur_partition>   Current partition index, 1-based\n"
                   "  --list-tests         List selected tests without executing\n"
                   "  --test <name>        Select an exact test description\n"
                   "  --allow-empty        Permit zero selected tests\n"
                   "  --junit <path>       Write JUnit XML\n"
                   "  --recover-abort, -r  Best-effort in-process SIGABRT recovery\n"
                   "  -h                   Show this help text\n",
                   total_num_tests, argv[0]);
            return 0;

        default:
            fprintf(stderr, "Unknown arguments: run with -h for help\n");
            return EXIT_FAILURE;
        }
    }

    }

    if (total_num_tests == 0 && !allow_empty) {
        fprintf(stderr, "Zero test cases were found! This usually means that "
                        "something went wrong with test registration.\n");
        return EXIT_FAILURE;
    }

    // Filter out tests
    struct baro__test_list tests;
    if (raw_tag_filters != NULL && raw_tag_filters[0] != '\0') {
        baro__test_list_create(&tests,baro__c.tests.size);

        // Parse the filter list
        size_t num_filters = 1;
        char *p = raw_tag_filters;
        while (*p) {
            if (*p++ == ',') {
                num_filters++;
            }
        }

        char **filters = malloc(num_filters * sizeof(char *));

        char* next_token = NULL;
#ifdef _WIN32
        p = strtok_s(raw_tag_filters, ",", &next_token);
#else
        p = strtok_r(raw_tag_filters, ",", &next_token);
#endif
        for (size_t i = 0; i < num_filters; i++) {
            if (p == NULL) {
                filters[i] = NULL;
                continue;
            }

            // Skip whitespace
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                p++;
            }

            size_t const filter_len = strlen(p);
            if (filter_len == 0) {
                fprintf(stderr, "Invalid filter list\n");
                return EXIT_FAILURE;
            }

            filters[i] = malloc(filter_len + 2 + 1);
            snprintf(filters[i], filter_len + 2 + 1, "[%s]", p);

#ifdef _WIN32
            p = strtok_s(NULL, ",", &next_token);
#else
            p = strtok_r(NULL, ",", &next_token);
#endif
        }

        // Copy over tests that match the filters
        for (size_t i = 0; i < total_num_tests; i++) {
            struct baro__test const *test = &baro__c.tests.tests[i];

            for (size_t j = 0; j < num_filters; j++) {
                char const *filter = filters[j];
                if (filter != NULL && strstr(test->tag->desc, filter) != NULL) {
                    baro__test_list_add(&tests, test);
                    break;
                }
            }
        }

        // Clean up
        for (size_t i = 0; i < num_filters; i++) {
            if (filters[i] != NULL) {
                free(filters[i]);
            }
        }
        free(filters);

        free(raw_tag_filters);
        raw_tag_filters = NULL;
    } else {
        memcpy(&tests, &baro__c.tests, sizeof(baro__c.tests));
    }

    if (exact_name) {
        if (tests.tests == baro__c.tests.tests) {
            baro__test_list_create(&tests, total_num_tests ? total_num_tests : 1);
            for (size_t i = 0; i < total_num_tests; i++)
                baro__test_list_add(&tests, &baro__c.tests.tests[i]);
        }
        size_t selected = 0;
        for (size_t i = 0; i < tests.size; i++)
            if (!strcmp(tests.tests[i].tag->desc, exact_name)) tests.tests[selected++] = tests.tests[i];
        tests.size = selected;
    }
    if (!tests.size && !allow_empty) {
        fprintf(stderr, "No tests matched the selection\n");
        if (tests.tests != baro__c.tests.tests) free(tests.tests);
        return EXIT_FAILURE;
    }
    size_t const num_tests = tests.size;
    if (num_tests > 0 && (num_partitions < 1 || num_partitions > num_tests)) {
        fprintf(stderr, "Invalid number of partitions %zu, value should be"
                        "between 1 and %zu\n", num_partitions, num_tests);
        return EXIT_FAILURE;
    }

    if (cur_partition < 1 || cur_partition > num_partitions) {
        fprintf(stderr, "Invalid current partition %zu, value should between 1"
                        " and %zu inclusive\n", cur_partition, num_partitions);
        return EXIT_FAILURE;
    }

    // Sort the list of tests so that we get a deterministic order of execution
    // across different compilers and runtimes
    baro__test_list_sort(&tests);

    // Partition the tests if we are in a multiprocess workflow
    size_t const partition_size = num_tests / num_partitions;
    size_t const remainder = num_tests % num_partitions;
    size_t const partition_index = cur_partition - 1;
    size_t const first_test = partition_size * partition_index +
            (partition_index < remainder ? partition_index : remainder);
    size_t const last_test = first_test + partition_size + (partition_index < remainder);

    if (list_tests) {
        for (size_t i = first_test; i < last_test; i++) puts(tests.tests[i].tag->desc);
        if (tests.tests != baro__c.tests.tests) free(tests.tests);
        return EXIT_SUCCESS;
    }
    struct baro__result *results = calloc(num_tests ? num_tests : 1, sizeof(*results));
    if (!results) return EXIT_FAILURE;

    size_t const num_tests_to_run = last_test - first_test;
    printf("Running %zu out of %zu test%s (of %zu total)\n", num_tests_to_run, num_tests,
           num_tests > 1 ? "s" : "", total_num_tests);
    if (num_partitions > 1) {
        printf("(Partition %zu: tests %zu through %zu)\n", cur_partition, first_test + 1, last_test);
    }

    printf(BARO__SEPARATOR);

    baro__c.suppress_stdout = suppress_stdout;
    baro__redirect_output(&baro__c, suppress_stdout);
    if (suppress_stderr) {
        baro__disable_output(&baro__c, stderr);
    }

    // Begin running tests serially
    for (size_t i = first_test; i < last_test; i++) {
        struct baro__test const * const test = &tests.tests[i];
        size_t const before_asserts = baro__c.num_asserts;
        size_t const before_failed = baro__c.num_asserts_failed;
        baro__c.current_test = test;
        baro__c.current_test_failed = 0;
        baro__hash_set_clear(&baro__c.passed_subtests);

        int run_test = 1;

        int const jmp_val = BARO__SETJMP(baro__c.env);
        // Recover from REQUIRE assertion failures
        if (jmp_val == BARO__JMP_REQUIRE) {
            run_test = 0;
        }
        // Recover from SIGABRT failures
        else if (jmp_val == BARO__JMP_SIGABRT) {
            baro__c.current_test_failed = 1;
            baro__c.num_asserts++;
            baro__c.num_asserts_failed++;

            baro__redirect_output(&baro__c, 0);

            printf(BARO__RED "Assertion failed! Caught SIGABRT\n" BARO__UNSET_COLOR);
            baro__assert_failed(BARO__ASSERT_REQUIRE, 0);

            run_test = 0;
        }
        // Otherwise, install a SIGABRT handler
        else {
            if (recover_abort) set_sigabrt_handler(handle_signal);
        }

        while (run_test) {
            // Reset the current subtest stack
            baro__c.should_reenter_subtest = 0;
            baro__c.subtest_max_size = 0;
            baro__tag_list_clear(&baro__c.subtest_stack);

            test->func();
            baro__run_cleanups();

            // Keep looping until all subtest permutations have been visited
            if (!baro__c.should_reenter_subtest) {
                run_test = 0;
            }
        }

        baro__run_cleanups();
        if (recover_abort) set_sigabrt_handler(NULL);
        results[i].ran = 1;
        results[i].failed = baro__c.current_test_failed;
        results[i].asserts = baro__c.num_asserts - before_asserts;
        results[i].asserts_failed = baro__c.num_asserts_failed - before_failed;
        baro__c.num_tests_ran++;
        if (baro__c.current_test_failed) {
            baro__c.num_tests_failed++;
            if (stop_after_failure) {
                break;
            }
        } else if (show_passed_tests) {
            baro__redirect_output(&baro__c, 0);

            printf(BARO__GREEN "Passed: %s (%s:%d)\n" BARO__UNSET_COLOR BARO__SEPARATOR,
                   test->tag->desc, extract_file_name(test->tag->file_path), test->tag->line_num);
            baro__redirect_output(&baro__c, suppress_stdout);
        }

        // Discard successful output and start the next test with empty capture.
        baro__redirect_output(&baro__c, 0);
        baro__c.stdout_size = 0;
        baro__redirect_output(&baro__c, suppress_stdout);
    }

    baro__redirect_output(&baro__c, 0);

    printf("tests:   %5zu total | " BARO__GREEN "%5zu passed" BARO__UNSET_COLOR
           " | " BARO__RED "%5zu failed" BARO__UNSET_COLOR "\n",
           baro__c.num_tests_ran, baro__c.num_tests_ran - baro__c.num_tests_failed,
           baro__c.num_tests_failed);

    printf("asserts: %5zu total | " BARO__GREEN "%5zu passed" BARO__UNSET_COLOR
           " | " BARO__RED "%5zu failed" BARO__UNSET_COLOR "\n",
           baro__c.num_asserts, baro__c.num_asserts - baro__c.num_asserts_failed,
           baro__c.num_asserts_failed);

    int report_ok = !junit_path || baro__junit(junit_path, &tests, results);
    free(results);
    if (tests.tests != baro__c.tests.tests) free(tests.tests);
    free(raw_tag_filters);
    return baro__c.num_tests_failed || !report_ok ? EXIT_FAILURE : EXIT_SUCCESS;
}

int baro_is_child(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--baro-child") == 0) return 1;
    }
    return 0;
}

static void baro__typed_failure(int hard, const char *file, int line, const char *values) {
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;
    baro__redirect_output(&baro__c, 0);
    printf("%s failed: %s\nAt %s:%d\n", hard ? "Require" : "Check", values, extract_file_name(file), line);
    baro__assert_failed(hard ? BARO__ASSERT_REQUIRE : BARO__ASSERT_CHECK, 1);
}
void baro__typed_int(intmax_t a, intmax_t b, int hard, const char *file, int line) {
    baro__c.num_asserts++;
    if (a == b) return;
    char text[160]; snprintf(text, sizeof(text), "%" PRIdMAX " == %" PRIdMAX, a, b);
    baro__typed_failure(hard, file, line, text);
}
void baro__typed_uint(uintmax_t a, uintmax_t b, int hard, const char *file, int line) {
    baro__c.num_asserts++;
    if (a == b) return;
    char text[160]; snprintf(text, sizeof(text), "%" PRIuMAX " == %" PRIuMAX, a, b);
    baro__typed_failure(hard, file, line, text);
}
void baro__typed_ptr(const void *a, const void *b, int hard, const char *file, int line) {
    baro__c.num_asserts++;
    if (a == b) return;
    char text[160]; snprintf(text, sizeof(text), "%p == %p", (void *)a, (void *)b);
    baro__typed_failure(hard, file, line, text);
}
void baro__typed_double(double a, double b, int hard, const char *file, int line) {
    baro__c.num_asserts++;
    if (a == b) return;
    char text[160]; snprintf(text, sizeof(text), "%.17g == %.17g", a, b);
    baro__typed_failure(hard, file, line, text);
}
void baro__near(double a, double b, double absolute, double relative,
                int hard, const char *file, int line) {
    baro__c.num_asserts++;
    double scale = fmax(fabs(a), fabs(b));
    int valid = isfinite(absolute) && isfinite(relative) && absolute >= 0 && relative >= 0;
    if (valid && (a == b || (isfinite(a) && isfinite(b) &&
        (fabs(a - b) <= absolute || (scale > 0 && fabs(a / scale - b / scale) <= relative))))) return;
    char text[256];
    snprintf(text, sizeof(text), "%.17g near %.17g (absolute %.17g, relative %.17g)", a, b, absolute, relative);
    baro__typed_failure(hard, file, line, text);
}
