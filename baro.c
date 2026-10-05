// The runtime behind the macros in baro.h.
#ifndef BARO_ENABLE
#define BARO_ENABLE
#endif
#include "baro.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct baro__tag_list {
    struct baro__tag const **tags;
    size_t size;
    size_t capacity;
};

static inline void baro__tag_list_create(
        struct baro__tag_list * const list,
        size_t const capacity) {
    list->tags = calloc(capacity, sizeof(struct baro__tag *));
    list->size = 0;
    list->capacity = capacity;
}

static inline void baro__tag_list_clear(
        struct baro__tag_list * const list) {
    list->size = 0;
}

static inline void baro__tag_list_push(
        struct baro__tag_list * const list,
        struct baro__tag const * const tag) {
    if (list->size == list->capacity) {
        list->capacity *= 2;

        struct baro__tag const ** const old_tags = list->tags;

        list->tags = calloc(list->capacity, sizeof(struct baro__tag *));
        for (size_t i = 0; i < list->size; i++) {
            list->tags[i] = old_tags[i];
        }

        free(old_tags);
    }

    list->tags[list->size++] = tag;
}

static inline int baro__tag_list_pop(
        struct baro__tag_list * const list,
        struct baro__tag * const tag) {
    if (list->size == 0) {
        return 0;
    }

    if (tag) {
        *tag = *list->tags[list->size - 1];
    }
    list->size--;

    return 1;
}

// Use the address of each item in the list as build a hash, as we only
// expect tags to be statically allocated. The C99 standard guarantees that
// two named objects of the same type will not have the same memory
// location (6.5.9/6):
//
// > Two pointers compare equal if and only if both are null pointers,
// > both are pointers to the same object (including a pointer to an object
// > and a subobject at its beginning) or function, both are pointers to
// > one past the last element of the same array object, or one is a pointer
// > to one past the end of one array object and the other is a pointer to
// > the start of a different array object that happens to immediately
// > follow the first array object in the address space.
static inline uint64_t baro__tag_hash(
        struct baro__tag const * const tag,
        size_t const index) {
    // Adapted from MurmurHash3's avalanche mixer. The hash of a list is the XOR
    // of its items' hashes, so it can be kept up to date as items come and go.
    uint64_t a = (uint64_t) tag + index;
    a ^= a >> 33u;
    a *= 0xff51afd7ed558ccdL;
    a ^= a >> 33u;
    a *= 0xc4ceb9fe1a85ec53L;
    a ^= a >> 33u;
    return a;
}

struct baro__hash_set {
    size_t nbits;
    uint64_t mask;

    size_t capacity;
    size_t size;

    uint64_t *hashes;
};

static inline void baro__hash_set_create(
        struct baro__hash_set * const set) {
    set->nbits = 4;
    set->capacity = 1llu << set->nbits;
    set->mask = set->capacity - 1;

    set->hashes = calloc(set->capacity, sizeof(uint64_t));
    set->size = 0;
}

static inline void baro__hash_set_clear(
        struct baro__hash_set * const set) {
    set->size = 0;
    memset(set->hashes, 0, set->capacity * sizeof(uint64_t));
}

static const uint64_t baro__hash_set_prime_1 = 73;
static const uint64_t baro__hash_set_prime_2 = 5009;

static inline void baro__hash_set_add_only(
        struct baro__hash_set * const set,
        uint64_t const hash) {
    uint64_t index = set->mask & (baro__hash_set_prime_1 * hash);

    while (set->hashes[index]) {
        if (set->hashes[index] == hash) {
            return;
        }

        index = set->mask & (index + baro__hash_set_prime_2);
    }

    set->size++;
    set->hashes[index] = hash;
}

static inline void baro__hash_set_grow(
        struct baro__hash_set * const set) {
    if ((int) set->size > (double) set->capacity * 0.85) {
        uint64_t *prev_hashes = set->hashes;
        size_t prev_capacity = set->capacity;

        set->nbits++;
        set->capacity = 1llu << set->nbits;
        set->mask = set->capacity - 1;

        set->hashes = calloc(set->capacity, sizeof(uint64_t));
        set->size = 0;
        for (size_t i = 0; i < prev_capacity; i++) {
            if (prev_hashes[i] == 0) {
                continue;
            }

            baro__hash_set_add_only(set, prev_hashes[i]);
        }
        free(prev_hashes);
    }
}

static inline void baro__hash_set_add(
        struct baro__hash_set * const set,
        uint64_t const hash) {
    baro__hash_set_add_only(set, hash);
    baro__hash_set_grow(set);
}

static inline int baro__hash_set_contains(
        struct baro__hash_set * const set,
        uint64_t const hash) {
    uint64_t index = set->mask & (baro__hash_set_prime_1 * hash);

    while (set->hashes[index]) {
        if (set->hashes[index] == hash) {
            return 1;
        }

        index = set->mask & (index + baro__hash_set_prime_2);
    }

    return 0;
}

struct baro__test {
    size_t id;
    int expect_abort;
    const struct baro__tag *tag;

    void (*func)(void);
};

struct baro__test_list {
    struct baro__test *tests;
    size_t size;
    size_t capacity;
};

static inline void baro__test_list_create(
        struct baro__test_list * const list,
        size_t const capacity) {
    list->tests = calloc(capacity, sizeof(struct baro__test));
    list->size = 0;
    list->capacity = capacity;
}

static inline void baro__test_list_add(
        struct baro__test_list * const list,
        struct baro__test const * const test) {
    if (list->size == list->capacity) {
        list->capacity *= 2;

        struct baro__test *old_tests = list->tests;

        list->tests = calloc(list->capacity, sizeof(struct baro__test));
        for (size_t i = 0; i < list->size; i++) {
            list->tests[i] = old_tests[i];
        }

        free(old_tests);
    }

    list->tests[list->size++] = *test;
}

static inline int baro__test_list_sort_cmp(
        void const *lhs,
        void const *rhs) {
    struct baro__test const * const lhs_test = lhs;
    struct baro__test const * const rhs_test = rhs;

    int const file_name_cmp = strcmp(lhs_test->tag->file_path, rhs_test->tag->file_path);
    if (file_name_cmp != 0) {
        return file_name_cmp;
    }

    return lhs_test->tag->line_num - rhs_test->tag->line_num;
}

static inline void baro__test_list_sort(
        struct baro__test_list * const list) {
    qsort(list->tests, list->size, sizeof(list->tests[0]), baro__test_list_sort_cmp);
}

// Maximum number of bytes to record from stdout per test
#define BARO__STDOUT_BUF_SIZE 4096

struct baro__cleanup {
    struct baro__cleanup *next;
    void (*callback)(void *);
    void *data;
};

struct baro__context {
    struct baro__cleanup *cleanups;
    struct baro__cleanup *active_cleanup;
    struct baro__test_list tests;
    struct baro__test const *current_test;
    int current_test_failed;

    size_t num_tests_ran;
    size_t num_tests_failed;

    size_t num_asserts;
    size_t num_asserts_failed;

    // A stack that updates as we enter and exit subtests. This is mainly
    // used to build "stack traces" for assertion failures.
    struct baro__tag_list subtest_stack;
    // Hash of subtest_stack, maintained incrementally.
    uint64_t subtest_hash;
    // Number of the top-level test being run, for baro__subtest::done_run.
    size_t run;
    // A set of all visited subtests in the current test, stored as 64-bit
    // hashes of the terminating subtest stacks.
    struct baro__hash_set passed_subtests;
    size_t subtest_max_size;
    int should_reenter_subtest;
    int subtest_entered;
    // First subtest found to have been left without reaching its end.
    struct baro__tag const *escaped_subtest;

#ifdef _WIN32
    jmp_buf env;
#else
    sigjmp_buf env;
#endif

    int compiler_diagnostics;
    int suppress_stdout;
    int real_stdout;
    FILE *stdout_capture;
    size_t stdout_size;
    char stdout_buffer[BARO__STDOUT_BUF_SIZE];
};

static struct baro__context baro__c;
static volatile sig_atomic_t baro__failure_pending;

static inline void baro__context_create(
        struct baro__context * const context) {
    baro__test_list_create(&context->tests, 128);
    context->current_test = NULL;
    context->current_test_failed = 0;

    context->num_tests_ran = context->num_tests_failed = 0;
    context->num_asserts = context->num_asserts_failed = 0;

    baro__tag_list_create(&context->subtest_stack, 8);
    context->subtest_hash = 0;
    context->run = 0;
    baro__hash_set_create(&context->passed_subtests);
    context->subtest_max_size = 0;
    context->should_reenter_subtest = 0;
    context->subtest_entered = 0;
    context->escaped_subtest = NULL;

    context->suppress_stdout = 1;
    context->real_stdout = -1;
    context->stdout_capture = NULL;
    context->stdout_size = 0;
    memset(context->stdout_buffer, 0, BARO__STDOUT_BUF_SIZE);
}

#ifdef _WIN32
#include <io.h>

#define BARO__DUP _dup
#define BARO__DUP2 _dup2
#define BARO__CLOSE _close
#define BARO__FILENO _fileno
#define BARO__STRCASECMP _stricmp
#define BARO__SETJMP(env) setjmp(env)
#define BARO__LONGJMP(env, value) longjmp(env, value)
#else
#include <unistd.h>
#include <strings.h>
#define BARO__DUP dup
#define BARO__DUP2 dup2
#define BARO__CLOSE close
#define BARO__FILENO fileno
#define BARO__STRCASECMP strcasecmp
#define BARO__SETJMP(env) sigsetjmp(env, 1)
#define BARO__LONGJMP(env, value) siglongjmp(env, value)
#endif

void baro_defer(void (*callback)(void *), const void *data, size_t size) {
    struct baro__cleanup *entry = malloc(sizeof(*entry));
    if (!entry || !callback || (size && !data)) {
        fprintf(stderr, "Invalid cleanup registration or out of memory\n");
        exit(EXIT_FAILURE);
    }
    entry->data = malloc(size ? size : 1);
    if (!entry->data) { free(entry); fprintf(stderr, "Out of memory\n"); exit(EXIT_FAILURE); }
    entry->callback = callback;
    if (size) memcpy(entry->data, data, size);
    entry->next = baro__c.cleanups;
    baro__c.cleanups = entry;
}

static inline void baro__run_cleanups(void) {
    // An assertion in a callback may jump back to the runner. Retire it first.
    if (baro__c.active_cleanup) free(baro__c.active_cleanup->data);
    free(baro__c.active_cleanup);
    baro__c.active_cleanup = NULL;
    while (baro__c.cleanups) {
        struct baro__cleanup *entry = baro__c.cleanups;
        baro__c.cleanups = entry->next;
        baro__c.active_cleanup = entry;
        entry->callback(entry->data);
        free(entry->data);
        free(entry);
        baro__c.active_cleanup = NULL;
    }
}

static inline void baro__disable_output(
        struct baro__context * const context,
        FILE *file) {
    (void)context;
#ifdef _WIN32
    FILE *dummy;
        if (freopen_s(&dummy, "NUL", "a", file) != 0) {
#else
    if (freopen("/dev/null", "a", file) == NULL) {
#endif
        fprintf(stderr, "Failed to disable output to fileno %d\n", BARO__FILENO(file));
        exit(1);
    }
    setvbuf(file, NULL, _IONBF, 0);
}

// Capture into a temporary file, independent of libc's private stream buffer.
// On restoration retain only the last BARO__STDOUT_BUF_SIZE bytes.
static inline void baro__redirect_output(
        struct baro__context * const context,
        int const enable) {
    if (enable && context->real_stdout == -1) {
        if (fflush(stdout) != 0) {
            perror("Failed to flush stdout");
            exit(EXIT_FAILURE);
        }
        context->stdout_capture = tmpfile();
        context->real_stdout = BARO__DUP(BARO__FILENO(stdout));
        if (!context->stdout_capture || context->real_stdout == -1 ||
                BARO__DUP2(BARO__FILENO(context->stdout_capture), BARO__FILENO(stdout)) == -1) {
            perror("Failed to capture stdout");
            exit(EXIT_FAILURE);
        }
        context->stdout_size = 0;
    } else if (!enable && context->real_stdout != -1) {
        if (fflush(stdout) != 0 ||
                BARO__DUP2(context->real_stdout, BARO__FILENO(stdout)) == -1) {
            perror("Failed to restore stdout");
            exit(EXIT_FAILURE);
        }
        BARO__CLOSE(context->real_stdout);
        context->real_stdout = -1;

        FILE *capture = context->stdout_capture;
        if (fseek(capture, 0, SEEK_END) != 0) {
            perror("Failed to seek captured output");
            exit(EXIT_FAILURE);
        }
        long const end = ftell(capture);
        if (end < 0 || fseek(capture,
                end > BARO__STDOUT_BUF_SIZE ? end - BARO__STDOUT_BUF_SIZE : 0,
                SEEK_SET) != 0) {
            perror("Failed to seek captured output");
            exit(EXIT_FAILURE);
        }
        context->stdout_size = fread(context->stdout_buffer, 1,
                                    BARO__STDOUT_BUF_SIZE, capture);
        if (ferror(capture)) {
            perror("Failed to read captured output");
            exit(EXIT_FAILURE);
        }
        fclose(capture);
        context->stdout_capture = NULL;
    }
}

void baro__register_test_kind(
        void (* const test_func)(void),
        struct baro__tag const * const tag, int expect_abort) {
    if (baro__c.tests.size == 0) {
        baro__context_create(&baro__c);
    }

    struct baro__test const test = {.expect_abort = expect_abort, .func = test_func, .tag = tag};
    baro__test_list_add(&baro__c.tests, &test);
}

static void baro__subtest_error(struct baro__tag const *tag, char const *message);

int baro__check_subtest(
        struct baro__subtest * const site) {
    size_t const depth = baro__c.subtest_stack.size;
    if (depth < baro__c.subtest_max_size) {
        // A traversal enters one subtest per depth, and its slot is not reused
        // until the next traversal. Meeting that subtest again (from a loop or
        // a repeated helper call) means every later visit is silently skipped.
        if (baro__c.subtest_stack.tags[depth] == &site->tag) {
            baro__subtest_error(&site->tag, "subtest reached more than once in a traversal; "
                                            "later visits do not run");
            return 0;
        }

        baro__c.should_reenter_subtest = 1;
        return 0;
    }

    // Every traversal passes all earlier siblings again, so the common case of
    // a subtest that already finished under this parent must stay cheap.
    if (site->done_run == baro__c.run && site->done_parent == baro__c.subtest_hash) {
        return 0;
    }

    // The same statement can also finish under several parents (a helper
    // called from different subtests); the set remembers all of them.
    uint64_t const hash = baro__c.subtest_hash ^ baro__tag_hash(&site->tag, depth);
    if (baro__hash_set_contains(&baro__c.passed_subtests, hash)) {
        return 0;
    }

    baro__tag_list_push(&baro__c.subtest_stack, &site->tag);
    baro__c.subtest_hash = hash;
    baro__c.subtest_max_size = baro__c.subtest_stack.size;
    baro__c.subtest_entered = 1;
    return 1;
}

static inline void baro__pop_subtest(void) {
    struct baro__tag_list * const stack = &baro__c.subtest_stack;
    if (stack->size) {
        baro__c.subtest_hash ^= baro__tag_hash(stack->tags[stack->size - 1], stack->size - 1);
        baro__tag_list_pop(stack, NULL);
    }
}

void baro__exit_subtest(
        struct baro__subtest * const site) {
    if (baro__c.subtest_entered) {
        // Anything still above this subtest was left by break or goto.
        while (baro__c.subtest_stack.size > 1 &&
               baro__c.subtest_stack.tags[baro__c.subtest_stack.size - 1] != &site->tag) {
            if (!baro__c.escaped_subtest) {
                baro__c.escaped_subtest = baro__c.subtest_stack.tags[baro__c.subtest_stack.size - 1];
            }
            baro__pop_subtest();
        }

        uint64_t const hash = baro__c.subtest_hash;
        baro__pop_subtest();

        if (!baro__c.should_reenter_subtest) {
            baro__hash_set_add(&baro__c.passed_subtests, hash);
            site->done_run = baro__c.run;
            site->done_parent = baro__c.subtest_hash;
        }
    }
}

#define BARO__SEPARATOR "============================================================\n"





enum baro__jmp_val {
    BARO__JMP_REQUIRE = 1,
    BARO__JMP_SIGABRT,
};

static inline char const *baro__file_name(
        char const *path) {
    char const *last = path;
    for (char const *p = path; *p; p++) {
        if ((*p == '/' || *p == '\\' || *p == ':') && (p[1] != '\0')) {
            last = p + 1;
        }
    }
    return last;
}

static inline void baro__report_location(const char *file, int line) {
    if (baro__c.compiler_diagnostics)
        printf("%s:%d: error: assertion failed\n", file, line);
    else
        printf("At %s:%d\n", baro__file_name(file), line);
}

static inline void baro__assert_failed(
        enum baro__assert_type const type, int const jump) {
    struct baro__test const * const test = baro__c.current_test;
    printf("  In: %s (%s:%d)\n",
           test->tag->desc, baro__file_name(test->tag->file_path), test->tag->line_num);

    for (size_t i = 0; i < baro__c.subtest_stack.size; i++) {
        struct baro__tag const * const subtest_tag = baro__c.subtest_stack.tags[i];
        printf("%*cUnder: %s (%s:%d)\n", (int) (i + 2) * 2, ' ',
               subtest_tag->desc, baro__file_name(subtest_tag->file_path), subtest_tag->line_num);
    }

    if (baro__c.stdout_size) {
        printf("Captured output:\n");
        fwrite(baro__c.stdout_buffer, 1, baro__c.stdout_size, stdout);
        putchar('\n');
        baro__c.stdout_size = 0;
    }

    printf(BARO__SEPARATOR);

    baro__redirect_output(&baro__c, baro__c.suppress_stdout);

    if (type == BARO__ASSERT_REQUIRE && jump) {
        BARO__LONGJMP(baro__c.env, BARO__JMP_REQUIRE);
    }
}

void baro__assert1(
        size_t const value,
        char const * const value_str,
        enum baro__expected_value const expected_value,
        enum baro__assert_type const type,
        char const * const desc,
        char const * const file_path,
        int const line_num) {
    baro__c.num_asserts++;

    if ((value != 0) == (expected_value == BARO__EXPECTING_TRUE)) {
        return;
    }

    baro__failure_pending = 1;
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;

    baro__redirect_output(&baro__c, 0);

    char const * const assert_type = (type == BARO__ASSERT_REQUIRE ? "Require" : "Check");
    char const * const op = (expected_value == BARO__EXPECTING_TRUE ? " != 0" : " == 0");
    printf("%s failed:%s%s\n", assert_type, *desc ? " " : "", desc);
    printf("    %s%s\n", value_str, op);
    baro__report_location(file_path, line_num);

    baro__assert_failed(type, 1);
}

// Compare in the caller to preserve C operand types and evaluate each once.
// C99 has no portable type deduction for capturing and printing both values.
void baro__assert2(
        enum baro__assert_cond cond,
        int passed,
        char const *lhs_str,
        char const *rhs_str,
        enum baro__assert_type type,
        char const *desc,
        char const *file_path,
        int line_num) {
    baro__c.num_asserts++;

    if (passed) {
        return;
    }

    baro__failure_pending = 1;
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;

    baro__redirect_output(&baro__c, 0);

    char const * const op =
            cond == BARO__ASSERT_EQ ? "==" :
            cond == BARO__ASSERT_NE ? "!=" :
            cond == BARO__ASSERT_LT ? "<" :
            cond == BARO__ASSERT_LE ? "<=" :
            cond == BARO__ASSERT_GT ? ">" :
            cond == BARO__ASSERT_GE ? ">=" : "";

    char const * const assert_type = (type == BARO__ASSERT_REQUIRE ? "Require" : "Check");
    printf("%s failed:%s%s\n", assert_type, *desc ? " " : "", desc);
    printf("    %s %s %s\n", lhs_str, op, rhs_str);
    baro__report_location(file_path, line_num);

    baro__assert_failed(type, 1);
}

void baro__assert_str(
        char const *lhs,
        char const *lhs_str,
        char const *rhs,
        char const *rhs_str,
        enum baro__expected_value expected_value,
        enum baro__case_sensitivity case_sensitivity,
        enum baro__assert_type type,
        char const *desc,
        char const *file_path,
        int line_num) {
    baro__c.num_asserts++;

    // Two null pointers compare equal; null and a string compare unequal.
    int const equal = lhs == rhs || (lhs && rhs &&
            (case_sensitivity == BARO__CASE_SENSITIVE ? strcmp(lhs, rhs) :
                                                       BARO__STRCASECMP(lhs, rhs)) == 0);
    if (equal == (expected_value == BARO__EXPECTING_TRUE)) {
        return;
    }

    baro__failure_pending = 1;
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;

    baro__redirect_output(&baro__c, 0);

    char const * const op = (expected_value == BARO__EXPECTING_TRUE ? "==" : "!=");
    char const * const assert_type = (type == BARO__ASSERT_REQUIRE ? "Require" : "Check");
    char const * const sensitivity = (case_sensitivity == BARO__CASE_SENSITIVE ? "" : " (case insensitive)");

    char const * const lhs_wrap = (lhs ? "\"" : "");
    char const * const rhs_wrap = (rhs ? "\"" : "");
    if (!lhs) {
        lhs = "[null]";
    }
    if (!rhs) {
        rhs = "[null]";
    }

    size_t const str_len = strlen(lhs_str);
    size_t const expanded_len = strlen(lhs) + strlen(lhs_wrap) * 2;

    size_t str_padding = 0;
    size_t expanded_padding = 0;
    if (str_len > expanded_len) {
        expanded_padding = str_len - expanded_len;
    } else if (expanded_len > str_len) {
        str_padding = expanded_len - str_len;
    }

    printf("%s%s failed:%s%s\n", assert_type, sensitivity, *desc ? " " : "", desc);
    printf("    %s %*s%s %s\n", lhs_str, (int)str_padding, "", op, rhs_str);
    printf("==> %s%s%s %*s%s %s%s%s\n", lhs_wrap, lhs, lhs_wrap, (int)expanded_padding, "", op, rhs_wrap, rhs, rhs_wrap);
    baro__report_location(file_path, line_num);

    baro__assert_failed(type, 1);
}

void baro__assert_arr(
        uint8_t const *lhs,
        char const *lhs_str,
        uint8_t const *rhs,
        char const *rhs_str,
        size_t const element_size,
        size_t const element_count,
        enum baro__expected_value expected_value,
        enum baro__assert_type type,
        char const *desc,
        char const *file_path,
        int line_num) {
    baro__c.num_asserts++;

    size_t const size = element_size * element_count;

    if ((memcmp(lhs, rhs, size) == 0) == (expected_value == BARO__EXPECTING_TRUE)) {
        return;
    }

    size_t index = 0;
    for (; index < size; index++) {
        if ((lhs[index] == rhs[index] && expected_value == BARO__EXPECTING_FALSE) ||
                (lhs[index] != rhs[index] && expected_value == BARO__EXPECTING_TRUE)) {
            break;
        }
    }
    size_t const element_index = index / element_size;

    baro__failure_pending = 1;
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;

    baro__redirect_output(&baro__c, 0);

    char const * const op = (expected_value == BARO__EXPECTING_TRUE ? "==" : "!=");
    char const * const assert_type = (type == BARO__ASSERT_REQUIRE ? "Require" : "Check");

    // Build a hex literal of the array element
    size_t const val_str_size = element_size * 2 + element_size / 2 + 1;
    char *lhs_val_str = malloc(val_str_size);
    char *rhs_val_str = malloc(val_str_size);
    char *p = lhs_val_str;
    char *q = rhs_val_str;
    uint8_t const *r = &lhs[(element_index + 1) * element_size - 1];
    uint8_t const *s = &rhs[(element_index + 1) * element_size - 1];
    for (size_t i = 0; i < element_size; i++) {
        size_t const remaining = val_str_size - (p - lhs_val_str);

        p += snprintf(p, remaining, "%02x", *r--);
        q += snprintf(q, remaining, "%02x", *s--);

        // Add delimiters
        if (i % 2 == 1 && i + 1 < element_size) {
            *p++ = '_';
            *q++ = '_';
        }
    }
    *p = '\0';
    *q = '\0';

    printf("%s array failed:%s%s\n", assert_type, *desc ? " " : "", desc);
    printf("    %s[%zu] %s %s[%zu]\n", lhs_str, element_index, op, rhs_str, element_index);
    printf("==> 0x%s %s 0x%s\n", lhs_val_str, op, rhs_val_str);
    baro__report_location(file_path, line_num);

    free(lhs_val_str);
    free(rhs_val_str);

    baro__assert_failed(type, 1);
}

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

/* Match comma-separated tags directly in argv; no token copies or platform
 * tokenizer are needed. Empty separators are ignored, as with strtok. */
static int baro__matches_tags(const char *description, const char *filters) {
    if (!filters) return 1;
    int matched = 0;
    while (*filters) {
        filters += strspn(filters, ",");
        if (!*filters) break;
        size_t length = strcspn(filters, ",");
        const char *end = filters + length;
        filters += strspn(filters, " \t\n\r");
        if (filters >= end) return -1;
        length = (size_t)(end - filters);
        for (const char *tag = description; (tag = strchr(tag, '[')) != NULL; tag++) {
            if (!strncmp(tag + 1, filters, length) && tag[length + 1] == ']') matched = 1;
        }
        filters = end;
    }
    return matched;
}

struct baro__result {
    int ran, failed;
    size_t asserts, asserts_failed;
    const char *reason;
    double seconds;
};

static const char *baro__value(int *arg, int argc, char *argv[]) {
    const char *option = argv[*arg];
    if (++*arg < argc) return argv[*arg];
    fprintf(stderr, "Missing value for %s\n", option);
    return NULL;
}

#include "baro_process.h"

static volatile sig_atomic_t baro__child_result_fd = -1;
static void baro__child_abort(int signum) {
    (void)signum;
#ifdef _WIN32
    if (baro__failure_pending) _write(baro__child_result_fd, "BARO_ABORT_FAILED\n", 18);
    else _write(baro__child_result_fd, "BARO_ABORT\n", 11);
    _exit(128 + SIGABRT);
#else
    ssize_t ignored = baro__failure_pending ?
        write(baro__child_result_fd, "BARO_ABORT_FAILED\n", 18) :
        write(baro__child_result_fd, "BARO_ABORT\n", 11);
    (void)ignored;
    _exit(128 + SIGABRT);
#endif
}

// The adapter checks this private record AND the process exit status. Writes
// from the SIGABRT handler use only async-signal-safe operations on POSIX.
static volatile sig_atomic_t baro__ctest_result_fd = -1;
static int baro__ctest_complete(void) {
    if (baro__ctest_result_fd < 0) return 1; /* Direct debugger invocation. */
#ifdef _WIN32
    return _write(baro__ctest_result_fd, "BARO_CTEST_OK\n", 14) == 14;
#else
    return write(baro__ctest_result_fd, "BARO_CTEST_OK\n", 14) == 14;
#endif
}
static void baro__ctest_abort(int signum) {
    (void)signum;
    _exit(!baro__failure_pending && baro__ctest_complete() ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void baro__json_string(const char *text, size_t size) {
    putchar('"');
    for (size_t i = 0; i < size; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c < 32) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

static void baro__json_test(const struct baro__test *test) {
    printf("{\"id\":%zu,\"name\":", test->id);
    baro__json_string(test->tag->desc, strlen(test->tag->desc));
    printf(",\"file\":");
    baro__json_string(test->tag->file_path, strlen(test->tag->file_path));
    printf(",\"line\":%d,\"expect_abort\":%s,\"tags\":[", test->tag->line_num,
           test->expect_abort ? "true" : "false");
    int comma = 0;
    const char *cursor = test->tag->desc;
    while ((cursor = strchr(cursor, '[')) != NULL) {
        const char *end = strchr(++cursor, ']');
        if (!end) break;
        if (cursor != end) {
            if (comma++) putchar(',');
            baro__json_string(cursor, (size_t)(end - cursor));
        }
        cursor = end + 1;
    }
    printf("]}");
}

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

/* printf's decimal separator follows the locale a test may have set. */
static void baro__junit_time(FILE *out, double seconds) {
    unsigned long long milliseconds = (unsigned long long)(seconds * 1000 + 0.5);
    fprintf(out, " time=\"%llu.%03llu\">", milliseconds / 1000, milliseconds % 1000);
}

static int baro__junit(const char *path, struct baro__test_list *tests,
                       struct baro__result *results) {
    FILE *out = fopen(path, "wb");
    if (!out) { perror("Cannot write JUnit report"); return 0; }
    double total = 0;
    for (size_t i = 0; i < tests->size; i++) total += results[i].seconds;
    fprintf(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<testsuite name=\"baro\" tests=\"%zu\" failures=\"%zu\"",
                 baro__c.num_tests_ran, baro__c.num_tests_failed);
    baro__junit_time(out, total);
    fputc('\n', out);
    for (size_t i = 0; i < tests->size; i++) {
        if (!results[i].ran) continue;
        fputs("  <testcase name=\"", out);
        baro__xml(out, tests->tests[i].tag->desc);
        fputs("\" file=\"", out);
        baro__xml(out, tests->tests[i].tag->file_path);
        fprintf(out, "\" line=\"%d\" assertions=\"%zu\"", tests->tests[i].tag->line_num, results[i].asserts);
        baro__junit_time(out, results[i].seconds);
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

static void baro__subtest_error(struct baro__tag const *tag, char const *message) {
    baro__failure_pending = 1;
    baro__c.current_test_failed = 1;
    baro__c.num_asserts++;
    baro__c.num_asserts_failed++;

    baro__redirect_output(&baro__c, 0);

    printf("Subtest error: %s\n", message);
    printf("    %s\n", tag->desc);
    baro__report_location(tag->file_path, tag->line_num);
    baro__assert_failed(BARO__ASSERT_CHECK, 0);
}

static void baro__run_one(const struct baro__test *test, int recover_abort) {
    baro__c.current_test = test;
    baro__c.current_test_failed = 0;
    baro__failure_pending = 0;
    baro__c.run++;
    baro__hash_set_clear(&baro__c.passed_subtests);

    int run_test = 1;

    switch (BARO__SETJMP(baro__c.env)) {
    case BARO__JMP_REQUIRE:
        run_test = 0;
        break;
    case BARO__JMP_SIGABRT: {
        baro__failure_pending = 1;
        baro__c.current_test_failed = 1;
        baro__c.num_asserts++;
        baro__c.num_asserts_failed++;

        baro__redirect_output(&baro__c, 0);

        printf("Assertion failed! Caught SIGABRT\n");
        baro__assert_failed(BARO__ASSERT_REQUIRE, 0);

        run_test = 0;
        break;
    }
    default:
        if (recover_abort) set_sigabrt_handler(handle_signal);
        break;
    }

    while (run_test) {
        // Reset the current subtest stack
        baro__c.should_reenter_subtest = 0;
        baro__c.subtest_max_size = 0;
        baro__tag_list_clear(&baro__c.subtest_stack);
        baro__c.subtest_hash = 0;
        baro__c.escaped_subtest = NULL;

        test->func();
        baro__run_cleanups();

        // A subtest still on the stack was left without reaching its end, so
        // its siblings were skipped and it can never be marked as visited.
        if (baro__c.subtest_stack.size && !baro__c.escaped_subtest) {
            baro__c.escaped_subtest = baro__c.subtest_stack.tags[baro__c.subtest_stack.size - 1];
        }
        if (baro__c.escaped_subtest) {
            baro__subtest_error(baro__c.escaped_subtest,
                                "subtest left by return, break, or goto; "
                                "remaining subtests did not run");
            break;
        }

        // Keep looping until all subtest permutations have been visited
        if (!baro__c.should_reenter_subtest) {
            run_test = 0;
        }
    }

    baro__run_cleanups();
    if (recover_abort) set_sigabrt_handler(NULL);
}

static void baro__usage(const char *program, size_t total) {
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
           "  --jobs <count>       Concurrent isolated tests (default 1)\n"
           "  --timeout <seconds>  Isolated test deadline (s or ms suffix)\n"
           "  --isolate            Run each test in a fresh child process\n"
           "  --ctest              Single-test adapter mode (CTest owns process)\n"
           "  --test-id <id>       Select one test from this executable inventory\n"
           "  --list-tests-json    Print versioned JSON inventory\n"
           "  --diagnostics <mode> plain or compiler (clickable source locations)\n"
           "  --list-tests         List selected tests without executing\n"
           "  --test <name>        Select an exact test description\n"
           "  --allow-empty        Permit zero selected tests\n"
           "  --junit <path>       Write JUnit XML\n"
           "  --recover-abort, -r  Best-effort in-process SIGABRT recovery\n"
           "  -h, --help           Show this help text\n",
           total, program);
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
    const char *raw_tag_filters = NULL;
    const char *exact_name = NULL, *junit_path = NULL;
    int list_tests = 0, allow_empty = 0, isolate = 0;
    size_t child_id = 0, jobs = 1, test_id = 0;
    int compiler_diagnostics = 0, ctest_mode = 0;
    double timeout = 0;
    const char *child_result = NULL;
    char *baro__optarg = NULL;

    if (!baro__c.tests.tests) baro__context_create(&baro__c);
    baro__test_list_sort(&baro__c.tests);
    for (size_t i = 0; i < baro__c.tests.size; i++) baro__c.tests.tests[i].id = i + 1;
    size_t const total_num_tests = baro__c.tests.size;

    // Parse short clusters and long options without exporting getopt symbols.
    for (int arg = 1; arg < argc; arg++) {
        const char *option = argv[arg];
        if (strncmp(option, "--", 2) == 0) {
            if (!strcmp(option, "--ctest")) { ctest_mode = 1; continue; }
            if (!strcmp(option, "--help")) { baro__usage(argv[0], total_num_tests); return EXIT_SUCCESS; }
            if (!strcmp(option, "--test-id")) {
                if (!baro__value(&arg, argc, argv) || !(test_id = baro__positive(argv[arg]))) return EXIT_FAILURE;
                continue;
            }
            if (!strcmp(option, "--diagnostics")) {
                if (!baro__value(&arg, argc, argv)) return EXIT_FAILURE;
                if (!strcmp(argv[arg], "compiler")) compiler_diagnostics = 1;
                else if (!strcmp(argv[arg], "plain")) compiler_diagnostics = 0;
                else { fprintf(stderr, "Expected diagnostics: plain or compiler\n"); return EXIT_FAILURE; }
                continue;
            }
            if (!strcmp(option, "--list-tests-json")) { list_tests = 2; continue; }
            if (!strcmp(option, "--jobs") || !strcmp(option, "--timeout")) {
                if (!baro__value(&arg, argc, argv)) return EXIT_FAILURE;
                if (!strcmp(option, "--jobs")) { jobs = baro__positive(argv[arg]); if (!jobs) return EXIT_FAILURE; }
                else {
                    char *end; errno = 0; timeout = strtod(argv[arg], &end);
                    int valid = end != argv[arg] && (!*end || !strcmp(end, "s") || !strcmp(end, "ms"));
                    if (!strcmp(end, "ms")) timeout /= 1000;
                    if (!valid || errno || !isfinite(timeout) || timeout <= 0) {
                        fprintf(stderr, "Expected a positive duration such as 10, 10s or 500ms: %s\n", argv[arg]);
                        return EXIT_FAILURE;
                    }
                }
                continue;
            }
            if (!strcmp(option, "--isolate")) { isolate = 1; continue; }
            if (!strcmp(option, "--baro-child") || !strcmp(option, "--baro-result")) {
                if (!baro__value(&arg, argc, argv)) return EXIT_FAILURE;
                if (!strcmp(option, "--baro-child")) { child_id = baro__positive(argv[arg]); if (!child_id) return EXIT_FAILURE; }
                else child_result = argv[arg];
                continue;
            }
            if (!strcmp(option, "--list-tests")) { list_tests = 1; continue; }
            if (!strcmp(option, "--allow-empty")) { allow_empty = 1; continue; }
            if (!strcmp(option, "--recover-abort")) { recover_abort = 1; continue; }
            if (!strcmp(option, "--test") || !strcmp(option, "--junit")) {
                if (!baro__value(&arg, argc, argv)) return EXIT_FAILURE;
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
            if (!*baro__optarg) { fprintf(stderr, "Empty tag filter\n"); return EXIT_FAILURE; }
            raw_tag_filters = baro__optarg;
            break;

        case 'h':
            baro__usage(argv[0], total_num_tests);
            return 0;

        default:
            fprintf(stderr, "Unknown arguments: run with -h for help\n");
            return EXIT_FAILURE;
        }
    }

    }

    if (ctest_mode) suppress_stdout = 0; /* CTest captures this process output. */
    if (ctest_mode && (!test_id || isolate || recover_abort || child_id || child_result ||
        exact_name || raw_tag_filters || list_tests || junit_path || jobs != 1 || timeout > 0 ||
        cur_partition != 1 || num_partitions != 1)) {
        fprintf(stderr, "--ctest requires one --test-id and CTest-owned execution\n");
        return EXIT_FAILURE;
    }
    if (!isolate && (jobs != 1 || timeout > 0)) {
        fprintf(stderr, "--jobs and --timeout require --isolate\n"); return EXIT_FAILURE;
    }
    if (isolate && recover_abort) {
        fprintf(stderr, "--recover-abort is only for in-process tests\n"); return EXIT_FAILURE;
    }
    if (ctest_mode || child_id || recover_abort) baro__report_failures_without_dialogs();
    if (ctest_mode && getenv("BARO_CTEST_RESULT")) {
        FILE *channel = fopen(getenv("BARO_CTEST_RESULT"), "wb");
        if (!channel) return EXIT_FAILURE;
        baro__ctest_result_fd = BARO__DUP(BARO__FILENO(channel));
        fclose(channel);
        if (baro__ctest_result_fd < 0) return EXIT_FAILURE;
    }
    if (child_id || child_result) {
        if (!child_id || !child_result || child_id > total_num_tests || isolate || recover_abort ||
            raw_tag_filters || exact_name || test_id || list_tests || junit_path || num_partitions != 1 || cur_partition != 1) return EXIT_FAILURE;
        FILE *channel = fopen(child_result, "wb");
        if (!channel) return EXIT_FAILURE;
        baro__child_result_fd = BARO__DUP(BARO__FILENO(channel));
        fclose(channel);
        if (baro__child_result_fd < 0) return EXIT_FAILURE;
        signal(SIGABRT, baro__child_abort);
        setvbuf(stdout, NULL, _IONBF, 0);
    }

    if (total_num_tests == 0 && !allow_empty) {
        fprintf(stderr, "Zero test cases were found! This usually means that "
                        "something went wrong with test registration.\n");
        return EXIT_FAILURE;
    }

    // The registry is already sorted. Select once, preserving that order.
    if (baro__matches_tags("", raw_tag_filters) < 0) {
        fprintf(stderr, "Invalid filter list\n");
        return EXIT_FAILURE;
    }
    struct baro__test_list tests = baro__c.tests;
    if (raw_tag_filters || exact_name || test_id) {
        baro__test_list_create(&tests, total_num_tests ? total_num_tests : 1);
        for (size_t i = 0; i < total_num_tests; i++) {
            const struct baro__test *test = &baro__c.tests.tests[i];
            if (baro__matches_tags(test->tag->desc, raw_tag_filters) &&
                (!exact_name || !strcmp(test->tag->desc, exact_name)) &&
                (!test_id || test->id == test_id)) baro__test_list_add(&tests, test);
        }
    }
    if (!tests.size && !allow_empty) {
        fprintf(stderr, "No tests matched the selection\n");
        if (tests.tests != baro__c.tests.tests) free(tests.tests);
        return EXIT_FAILURE;
    }
    size_t const num_tests = tests.size;
    if (num_tests > 0 && (num_partitions < 1 || num_partitions > num_tests)) {
        fprintf(stderr, "Invalid number of partitions %zu, value should be"
                        " between 1 and %zu\n", num_partitions, num_tests);
        return EXIT_FAILURE;
    }

    if (cur_partition < 1 || cur_partition > num_partitions) {
        fprintf(stderr, "Invalid current partition %zu, value should be between 1"
                        " and %zu inclusive\n", cur_partition, num_partitions);
        return EXIT_FAILURE;
    }

    // Partition the tests if we are in a multiprocess workflow
    size_t const partition_size = num_tests / num_partitions;
    size_t const remainder = num_tests % num_partitions;
    size_t const partition_index = cur_partition - 1;
    size_t const first_test = child_id ? child_id - 1 : partition_size * partition_index +
            (partition_index < remainder ? partition_index : remainder);
    size_t const last_test = child_id ? child_id : first_test + partition_size + (partition_index < remainder);

    if (list_tests) {
        if (list_tests == 2) printf("{\"version\":1,\"tests\":[");
        for (size_t i = first_test; i < last_test; i++) {
            if (list_tests == 1) puts(tests.tests[i].tag->desc);
            else { if (i != first_test) putchar(','); baro__json_test(&tests.tests[i]); }
        }
        if (list_tests == 2) puts("]}");
        if (tests.tests != baro__c.tests.tests) free(tests.tests);
        return EXIT_SUCCESS;
    }
    if (!isolate && !child_id && !ctest_mode) {
        for (size_t i = first_test; i < last_test; i++) {
            if (tests.tests[i].expect_abort) {
                fprintf(stderr, "Expected-abort tests require --isolate\n");
                return EXIT_FAILURE;
            }
        }
    }
    struct baro__result *results = calloc(num_tests ? num_tests : 1, sizeof(*results));
    if (!results) return EXIT_FAILURE;

    size_t const num_tests_to_run = last_test - first_test;
    if (!child_id) {
    printf("Running %zu out of %zu test%s (of %zu total)\n", num_tests_to_run, num_tests,
           num_tests > 1 ? "s" : "", total_num_tests);
    if (num_partitions > 1) {
        printf("(Partition %zu: tests %zu through %zu)\n", cur_partition, first_test + 1, last_test);
    }

    printf(BARO__SEPARATOR);
    }

    baro__c.compiler_diagnostics = compiler_diagnostics;
    baro__c.suppress_stdout = suppress_stdout;
    if (!isolate) baro__redirect_output(&baro__c, suppress_stdout);
    if (suppress_stderr && !isolate) {
        baro__disable_output(&baro__c, stderr);
    }

    if (isolate) {
        char executable[4096];
        const char *exe = baro__executable(executable, sizeof(executable), argv[0]);
        struct baro__slot { struct baro__process process; size_t index, id; int active; };
        size_t workers = jobs < num_tests_to_run ? jobs : num_tests_to_run;
        struct baro__slot *slots = calloc(workers ? workers : 1, sizeof(*slots));
        if (!slots) return EXIT_FAILURE;
        size_t next = first_test, active = 0, idle = 0;
        int stopped = 0;
        while (active || (!stopped && next < last_test)) {
            for (size_t worker = 0; worker < workers; worker++) {
                struct baro__slot *slot = &slots[worker];
                if (!slot->active && !stopped && next < last_test) {
                    slot->index = next++;
                    slot->id = tests.tests[slot->index].id;
                    if (baro__process_start(&slot->process, exe, slot->id, compiler_diagnostics)) {
                        slot->active = 1; active++;
                    } else {
                        results[slot->index] = (struct baro__result){1, 1, 0, 0, "Could not launch test", 0};
                        baro__c.num_tests_ran++; baro__c.num_tests_failed++;
                        fprintf(stderr, "Failed to launch: %s\n", tests.tests[slot->index].tag->desc);
                        if (stop_after_failure) stopped = 1;
                    }
                }
                if (!slot->active || !baro__process_poll(&slot->process, timeout)) continue;
                size_t i = slot->index;
                results[i] = baro__process_result(&slot->process, slot->id);
                results[i].seconds = baro__monotonic() - slot->process.started;
                if (tests.tests[i].expect_abort) {
                    if (results[i].reason && !strcmp(results[i].reason, "SIGABRT")) {
                        results[i].failed = 0; results[i].reason = NULL;
                    } else {
                        results[i].failed = 1;
                        if (!results[i].reason) results[i].reason = "Expected SIGABRT was not raised";
                    }
                }
                if (!suppress_stdout || results[i].failed) baro__process_output(slot->process.out, stdout, suppress_stdout);
                if (!suppress_stderr) baro__process_output(slot->process.err, stderr, 0);
                baro__process_dispose(&slot->process);
                slot->active = 0; active--; idle = 0;
                baro__c.num_tests_ran++;
                baro__c.num_tests_failed += results[i].failed;
                baro__c.num_asserts += results[i].asserts;
                baro__c.num_asserts_failed += results[i].asserts_failed;
                if (results[i].failed || show_passed_tests)
                    printf("%s: %s%s%s\n", results[i].failed ? "Failed" : "Passed",
                           tests.tests[i].tag->desc, results[i].reason ? " - " : "",
                           results[i].reason ? results[i].reason : "");
                if (results[i].failed && stop_after_failure) stopped = 1;
            }
            if (active) baro__process_pause(idle++);
        }
        free(slots);
    } else {
        // Begin running tests serially
        for (size_t i = first_test; i < last_test; i++) {
            struct baro__test const * const test = &tests.tests[i];
            size_t const before_asserts = baro__c.num_asserts;
            size_t const before_failed = baro__c.num_asserts_failed;
            if (ctest_mode && test->expect_abort) signal(SIGABRT, baro__ctest_abort);
            double const started = baro__monotonic();
            baro__run_one(test, recover_abort);
            results[i].seconds = baro__monotonic() - started;
            if (ctest_mode && test->expect_abort) {
                signal(SIGABRT, SIG_DFL);
                baro__c.current_test_failed = 1;
                baro__failure_pending = 1;
                baro__redirect_output(&baro__c, 0);
                printf("Expected SIGABRT was not raised\n");
                baro__report_location(test->tag->file_path, test->tag->line_num);
            }
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

                printf("Passed: %s (%s:%d)\n" BARO__SEPARATOR,
                       test->tag->desc, baro__file_name(test->tag->file_path), test->tag->line_num);
                baro__redirect_output(&baro__c, suppress_stdout);
            }

            // Discard successful output and start the next test with empty capture.
            baro__redirect_output(&baro__c, 0);
            baro__c.stdout_size = 0;
            baro__redirect_output(&baro__c, suppress_stdout);
        }

    }
    baro__redirect_output(&baro__c, 0);
    if (child_id) {
#ifdef _WIN32
        FILE *channel = _fdopen(baro__child_result_fd, "wb");
#else
        FILE *channel = fdopen(baro__child_result_fd, "wb");
#endif
        if (!channel) return EXIT_FAILURE;
        fprintf(channel, "BARO1 %zu %d %zu %zu\n", child_id, baro__c.num_tests_failed != 0,
                baro__c.num_asserts, baro__c.num_asserts_failed);
        int ok = !ferror(channel);
        if (fclose(channel)) ok = 0;
        free(results);
        return !ok || baro__c.num_tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    printf("tests:   %5zu total | %5zu passed | %5zu failed\n",
           baro__c.num_tests_ran, baro__c.num_tests_ran - baro__c.num_tests_failed,
           baro__c.num_tests_failed);

    printf("asserts: %5zu total | %5zu passed | %5zu failed\n",
           baro__c.num_asserts, baro__c.num_asserts - baro__c.num_asserts_failed,
           baro__c.num_asserts_failed);

    int report_ok = !junit_path || baro__junit(junit_path, &tests, results);
    free(results);
    if (tests.tests != baro__c.tests.tests) free(tests.tests);
    if (ctest_mode && !baro__c.num_tests_failed && report_ok)
        report_ok = baro__ctest_complete();
    if (baro__ctest_result_fd >= 0) {
        BARO__CLOSE(baro__ctest_result_fd);
        baro__ctest_result_fd = -1;
    }
    return baro__c.num_tests_failed || !report_ok ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int baro__has_mode(int argc, char *argv[], const char *mode, const char *alternate) {
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, mode) || (alternate && !strcmp(arg, alternate))) return 1;
        if (!strcmp(arg, "--test") || !strcmp(arg, "--test-id") || !strcmp(arg, "--junit") ||
            !strcmp(arg, "--diagnostics") || !strcmp(arg, "--jobs") || !strcmp(arg, "--timeout") ||
            !strcmp(arg, "--baro-child") || !strcmp(arg, "--baro-result")) { i++; continue; }
        if (arg[0] == '-' && arg[1] != '-') {
            for (const char *p = arg + 1; *p; p++) {
                if (*p == 't' || *p == 'n' || *p == 'p') { if (!p[1]) i++; break; }
            }
        }
    }
    return 0;
}

int baro_is_child(int argc, char *argv[]) {
    return baro__has_mode(argc, argv, "--baro-child", NULL);
}

int baro_is_discovery(int argc, char *argv[]) {
    return baro__has_mode(argc, argv, "--list-tests", "--list-tests-json");
}

static void baro__typed_failure(int hard, const char *file, int line, const char *values) {
    baro__failure_pending = 1;
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;
    baro__redirect_output(&baro__c, 0);
    printf("%s failed: %s\n", hard ? "Require" : "Check", values);
    baro__report_location(file, line);
    baro__assert_failed(hard ? BARO__ASSERT_REQUIRE : BARO__ASSERT_CHECK, 1);
}
static const char *baro__operator(enum baro__assert_cond cond) {
    return cond == BARO__ASSERT_EQ ? "==" : cond == BARO__ASSERT_NE ? "!=" :
           cond == BARO__ASSERT_LT ? "<" : cond == BARO__ASSERT_LE ? "<=" :
           cond == BARO__ASSERT_GT ? ">" : ">=";
}
// Same layout as the string assertions: the source expression, then its values.
static void baro__values_failed(enum baro__assert_cond cond, const char *lhs_str, const char *rhs_str,
                                const char *lhs_value, const char *rhs_value,
                                enum baro__assert_type type, const char *desc,
                                const char *file, int line) {
    baro__failure_pending = 1;
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;
    baro__redirect_output(&baro__c, 0);
    const char *op = baro__operator(cond);
    printf("%s failed:%s%s\n",
           type == BARO__ASSERT_REQUIRE ? "Require" : "Check", *desc ? " " : "", desc);
    printf("    %s %s %s\n", lhs_str, op, rhs_str);
    if (lhs_value) printf("==> %s %s %s\n", lhs_value, op, rhs_value);
    baro__report_location(file, line);
    baro__assert_failed(type, 1);
}

#define BARO__HOLDS(cond, a, b) \
    ((cond) == BARO__ASSERT_EQ ? (a) == (b) : (cond) == BARO__ASSERT_NE ? (a) != (b) : \
     (cond) == BARO__ASSERT_LT ? (a) < (b) : (cond) == BARO__ASSERT_LE ? (a) <= (b) : \
     (cond) == BARO__ASSERT_GT ? (a) > (b) : (a) >= (b))
#define BARO__TYPED_FAILED(format) do { \
    char lhs[64], rhs[64]; \
    snprintf(lhs, sizeof(lhs), format, a); \
    snprintf(rhs, sizeof(rhs), format, b); \
    baro__values_failed(cond, a_str, b_str, lhs, rhs, \
                        hard ? BARO__ASSERT_REQUIRE : BARO__ASSERT_CHECK, "", file, line); \
} while (0)
void baro__typed_int(enum baro__assert_cond cond, intmax_t a, const char *a_str,
                     intmax_t b, const char *b_str, int hard, const char *file, int line) {
    baro__c.num_asserts++;
    if (BARO__HOLDS(cond, a, b)) return;
    BARO__TYPED_FAILED("%" PRIdMAX);
}
void baro__typed_uint(enum baro__assert_cond cond, uintmax_t a, const char *a_str,
                      uintmax_t b, const char *b_str, int hard, const char *file, int line) {
    baro__c.num_asserts++;
    if (BARO__HOLDS(cond, a, b)) return;
    BARO__TYPED_FAILED("%" PRIuMAX);
}
// Only equality is offered: ordering unrelated pointers is undefined.
void baro__typed_ptr(enum baro__assert_cond cond, const void *a, const char *a_str,
                     const void *b, const char *b_str, int hard, const char *file, int line) {
    baro__c.num_asserts++;
    if ((a == b) == (cond == BARO__ASSERT_EQ)) return;
    BARO__TYPED_FAILED("%p");
}
void baro__typed_double(enum baro__assert_cond cond, double a, const char *a_str,
                        double b, const char *b_str, int hard, const char *file, int line) {
    baro__c.num_asserts++;
    if (BARO__HOLDS(cond, a, b)) return;
    BARO__TYPED_FAILED("%.17g");
}

static int baro__format_value(char *text, size_t capacity, enum baro__value_kind kind,
                              const void *value, int pointer) {
#define BARO__FORMAT(kind, type, format) case kind: { \
    type typed; \
    memcpy(&typed, value, sizeof(typed)); \
    snprintf(text, capacity, format, typed); \
    return 1; \
}
    switch (kind) {
    BARO__FORMAT(BARO__VALUE_INT, int, "%d")
    BARO__FORMAT(BARO__VALUE_UINT, unsigned, "%u")
    BARO__FORMAT(BARO__VALUE_LONG, long, "%ld")
    BARO__FORMAT(BARO__VALUE_ULONG, unsigned long, "%lu")
    BARO__FORMAT(BARO__VALUE_LLONG, long long, "%lld")
    BARO__FORMAT(BARO__VALUE_ULLONG, unsigned long long, "%llu")
    BARO__FORMAT(BARO__VALUE_FLOAT, float, "%.9g")
    BARO__FORMAT(BARO__VALUE_DOUBLE, double, "%.17g")
    BARO__FORMAT(BARO__VALUE_LDOUBLE, long double, "%.21Lg")
    case BARO__VALUE_OTHER:
        if (pointer) {
            void *address;
            memcpy(&address, value, sizeof(address));
            snprintf(text, capacity, "%p", address);
            return 1;
        }
        break;
    case BARO__VALUE_NONE:
        break;
    }
#undef BARO__FORMAT
    return 0;
}
// The caller compares the operands in their own type; this only reports them.
void baro__assert_values(enum baro__assert_cond cond, int passed, enum baro__value_kind kind,
                         const void *lhs, const void *rhs, int pointer,
                         const char *lhs_str, const char *rhs_str, enum baro__assert_type type,
                         const char *desc, const char *file, int line) {
    baro__c.num_asserts++;
    if (passed) return;
    char lhs_value[64], rhs_value[64];
    int shown = baro__format_value(lhs_value, sizeof(lhs_value), kind, lhs, pointer) &&
                baro__format_value(rhs_value, sizeof(rhs_value), kind, rhs, pointer);
    baro__values_failed(cond, lhs_str, rhs_str, shown ? lhs_value : NULL, rhs_value, type, desc, file, line);
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
