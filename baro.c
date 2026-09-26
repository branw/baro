#include <signal.h>
#include "baro.h"

struct baro__context baro__c = {0};

static char *baro__optarg;

// A small getopt-like function for parsing short CLI arguments
static int baro__getopt(
        int const num_args,
        char * const * args,
        char const * opts) {
    static char *arg = "";
    static int cur_arg = 1;

    if (!*arg) {
        if (cur_arg >= num_args || *(arg = args[cur_arg]) != '-') {
            arg = "";
            return -1;
        }
        if (arg[1] && *++arg == '-') {
            ++cur_arg;
            arg = "";
            fprintf(stderr, "Long options not supported\n");
            return 0;
        }
    }

    char opt;
    char const *cur_opt;

    if ((opt = *arg++) == ':' || !(cur_opt = strchr(opts, opt))) {
        if (opt == '-') {
            return -1;
        }
        if (!*arg) {
            ++cur_arg;
        }
        fprintf(stderr, "Illegal option: %c\n", opt);
        return 0;
    }

    if (*++cur_opt != ':') {
        baro__optarg = NULL;
        if (!*arg) {
            ++cur_arg;
        }
    } else {
        if (*arg) {
            baro__optarg = arg;
        } else if (num_args <= ++cur_arg) {
            arg = "";
            fprintf(stderr, "Option requires an argument: %c\n", opt);
            return 0;
        } else {
            baro__optarg = args[cur_arg];
        }
        arg = "";
        ++cur_arg;
    }

    return opt;
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

    size_t const total_num_tests = baro__c.tests.size;

    // Parse command line options
    int c;
    while ((c = baro__getopt(argc, argv, "hraoesp:n:t:")) != -1) {
        switch (c) {
        case 'r': recover_abort = 1; break;
        case 'p':
            num_partitions = strtol(baro__optarg, NULL, 10);
            break;

        case 'n':
            cur_partition = strtol(baro__optarg, NULL, 10);
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
                   "  -h                   Show this help text\n",
                   total_num_tests, argv[0]);
            return 0;

        default:
            fprintf(stderr, "Unknown arguments: run with -h for help\n");
            return EXIT_FAILURE;
        }
    }

    if (total_num_tests == 0) {
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

    return baro__c.num_tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

int baro_is_child(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--baro-child") == 0) return 1;
    }
    return 0;
}
