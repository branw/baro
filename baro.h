#ifndef BARO_3FDC036FA2C64C72A0DB6BA1033C678B
#define BARO_3FDC036FA2C64C72A0DB6BA1033C678B

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

struct baro__tag {
  const char *desc;
  const char *file_name;
  int line_num;
};

struct baro__tag_list {
  struct baro__tag ** tags;
  int size;
  int capacity;
};

static inline void baro__tag_list_create(struct baro__tag_list *list, int capacity) {
  list->tags = calloc(capacity, sizeof(struct baro__tag *));
  list->size = 0;
  list->capacity = capacity;
}

static inline void baro__tag_list_destroy(struct baro__tag_list *list) {
  if (list->tags) {
    free(list->tags);
  }
}

static inline void baro__tag_list_clear(struct baro__tag_list *list) {
  list->size = 0;
}

static inline void baro__tag_list_push(struct baro__tag_list *list, struct baro__tag * tag) {
  if (list->size == list->capacity) {
    list->capacity *= 2;

    struct baro__tag **old_tags = list->tags;

    list->tags = calloc(list->capacity, sizeof(struct baro__tag *));
    for (size_t i = 0; i < list->size; i++) {
      list->tags[i] = old_tags[i];
    }

    free(old_tags);
  }

  list->tags[list->size++] = tag;
}

static inline int baro__tag_list_pop(struct baro__tag_list *list, struct baro__tag *tag) {
  if (list->size == 0) {
    return 0;
  }

  if (tag) {
    *tag = *list->tags[--list->size];
  }

  return 1;
}

static inline uint64_t baro__tag_list_hash(struct baro__tag_list *list) {
  uint64_t hash = 0;

  for (int i = 0; i < list->size; i++) {
    uint64_t a = (uint64_t)list->tags[i] + i;
    a ^= a >> 33u;
    a *= 0xff51afd7ed558ccdL;
    a ^= a >> 33u;
    a *= 0xc4ceb9fe1a85ec53L;
    a ^= a >> 33u;
    hash ^= a;
  }

  return hash;
}

struct baro__hash_set {
  size_t nbits;
  uint64_t mask;

  size_t capacity;
  size_t size;

  uint64_t *hashes;
};

static inline void baro__hash_set_create(struct baro__hash_set *set) {
  set->nbits = 4;
  set->capacity = 1llu << set->nbits;
  set->mask = set->capacity - 1;

  set->hashes = calloc(set->capacity, sizeof(uint64_t));
  set->size = 0;
}

static inline void baro__hash_set_clear(struct baro__hash_set *set) {
  set->size = 0;
  memset(set->hashes, 0, set->capacity * sizeof(uint64_t));
}

#define BARO__HASH_SET_PRIME_1 73
#define BARO__HASH_SET_PRIME_2 5009

static inline void baro__hash_set_add(struct baro__hash_set *set, uint64_t hash) {
  uint64_t index = set->mask & (BARO__HASH_SET_PRIME_1 * hash);

  while (set->hashes[index]) {
    if (set->hashes[index] == hash) {
      return;
    }

    index = set->mask & (index + BARO__HASH_SET_PRIME_2);
  }

  set->size++;
  set->hashes[index] = hash;

  if (set->size > (double) set->capacity * 0.85) {
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

      baro__hash_set_add(set, prev_hashes[i]);
    }
    free(prev_hashes);
  }
}

static inline int baro__hash_set_contains(struct baro__hash_set *set, uint64_t hash) {
  size_t index = set->mask & (BARO__HASH_SET_PRIME_1 * hash);

  while (set->hashes[index]) {
    if (set->hashes[index] == hash) {
      return 1;
    }

    index = set->mask & (index + BARO__HASH_SET_PRIME_2);
  }

  return 0;
}

struct baro__test {
  struct baro__tag *tag;
  void (*func)(void);
};

struct baro__test_list {
  struct baro__test *tests;
  size_t size;
  size_t capacity;
};

static inline void baro__test_list_create(struct baro__test_list *list, size_t capacity) {
  list->tests = calloc(capacity, sizeof(struct baro__test));
  list->size = 0;
  list->capacity = capacity;
}

static inline void baro__test_list_add(struct baro__test_list *list, struct baro__test *test) {
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

#define BARO__STDOUT_BUF_SIZE 4096

struct baro__context {
  struct baro__test_list tests;
  struct baro__test *current_test;
  int current_test_failed;

  size_t num_tests;
  size_t num_tests_failed;

  size_t num_asserts;
  size_t num_asserts_failed;

  struct baro__tag_list subtest_stack;
  struct baro__hash_set passed_subtests;
  int subtest_max_size;
  int should_reenter_subtest;
  int subtest_entered;

  jmp_buf env;

  int real_stdout;
  char stdout_buffer[BARO__STDOUT_BUF_SIZE];
};

extern struct baro__context baro__c;

static inline void baro__context_create(struct baro__context *context) {
  baro__test_list_create(&context->tests, 128);
  context->current_test = NULL;
  context->current_test_failed = 0;

  context->num_tests = context->num_tests_failed = 0;
  context->num_asserts = context->num_asserts_failed = 0;

  baro__tag_list_create(&context->subtest_stack, 8);
  baro__hash_set_create(&context->passed_subtests);
  context->subtest_max_size = 0;
  context->should_reenter_subtest = 0;
  context->subtest_entered = 0;

  context->real_stdout = -1;
  memset(context->stdout_buffer, 0, BARO__STDOUT_BUF_SIZE);
}

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define strcasecmp _stricmp
#else
#include <unistd.h>
#endif

static inline void baro__redirect_output(struct baro__context *context, int enable) {
  FILE *dummy = NULL;

  if (enable) {
    fflush(stdout);
    context->real_stdout = dup(1);
#ifdef _WIN32
    freopen_s(&dummy, "NUL", "a", stdout);
#else
    freopen("NUL", "a", stdout);
#endif
    setvbuf(stdout, baro__c.stdout_buffer, _IOFBF, BARO__STDOUT_BUF_SIZE);
  } else if (context->real_stdout != -1) {
#ifdef _WIN32
    freopen_s(&dummy, "NUL", "a", stdout);
#else
    freopen("NUL", "a", stdout);
#endif
    dup2(context->real_stdout, 1);
    setvbuf(stdout, NULL, _IONBF, 0);
  }
}

extern int baro__init;

static inline void baro__register_test(void (*test_func)(void), struct baro__tag * tag) {
  if (!baro__init) {
    baro__context_create(&baro__c);
    baro__init = 1;
  }

  struct baro__test test = {.func=test_func, .tag=tag};
  baro__test_list_add(&baro__c.tests, &test);
}

static inline int baro__check_subtest(struct baro__tag * const tag) {
  if (baro__c.subtest_stack.size < baro__c.subtest_max_size) {
    baro__c.should_reenter_subtest = 1;
    return 0;
  }

  baro__tag_list_push(&baro__c.subtest_stack, tag);
  if (baro__hash_set_contains(&baro__c.passed_subtests,
                              baro__tag_list_hash(&baro__c.subtest_stack))) {
    baro__tag_list_pop(&baro__c.subtest_stack, NULL);
    return 0;
  }

  baro__c.subtest_max_size = baro__c.subtest_stack.size;
  baro__c.subtest_entered = 1;
  return 1;
}

static inline void baro__exit_subtest() {
  if (baro__c.subtest_entered) {
    if (!baro__c.should_reenter_subtest) {
      baro__hash_set_add(&baro__c.passed_subtests, baro__tag_list_hash(&baro__c.subtest_stack));
    }

    baro__tag_list_pop(&baro__c.subtest_stack, NULL);
  }
}

#define BARO__SEPARATOR "============================================================\n"

enum baro__assert_type {
  BARO__ASSERT_EQ,
  BARO__ASSERT_NE,
  BARO__ASSERT_LT,
  BARO__ASSERT_LE,
  BARO__ASSERT_GT,
  BARO__ASSERT_GE,
};

static inline void baro__assert_failed(int required) {
  struct baro__test *test = baro__c.current_test;
  printf("  In: %s (%s:%d)\n",
         test->tag->desc, test->tag->file_name, test->tag->line_num);

  for (int i = 0; i < baro__c.subtest_stack.size; i++) {
    struct baro__tag *subtest_tag = baro__c.subtest_stack.tags[i];
    printf("%*cUnder: %s (%s:%d)\n", (i + 2) * 2, ' ',
           subtest_tag->desc, subtest_tag->file_name, subtest_tag->line_num);
  }

  if (baro__c.stdout_buffer[0]) {
    printf("Captured output:\n%s\n", baro__c.stdout_buffer);

    memset(baro__c.stdout_buffer, 0, BARO__STDOUT_BUF_SIZE);
  }

  printf(BARO__SEPARATOR);

  baro__redirect_output(&baro__c, 1);

  if (required) {
    longjmp(baro__c.env, 1);
  }
}

static inline void baro__assert1(size_t cond, char const *cond_str, int expected, int required,
                                 const char *desc, const char *file_name, int line_num) {
  baro__c.num_asserts++;

  int pass = (!!cond == !!expected);

  if (!pass) {
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;

    baro__redirect_output(&baro__c, 0);

    char const *assert_type = (required ? "Require" : "Check");
    char const *op_str = (expected ? "" : " false");
    char const *op = (expected ? " != 0" : " == 0");
    printf("%s%s failed: %s\n", assert_type, op_str, desc);
    printf("    %s%s\n", cond_str, op);
    printf("==> %zu%s\n", cond, op);
    printf("At %s:%d\n", file_name, line_num);

    baro__assert_failed(required);
  }
}

static inline void baro__assert2(enum baro__assert_type type, size_t lhs, const char *lhs_str,
                                size_t rhs, const char *rhs_str, int required, const char *desc,
                                const char *file_name, int line_num) {
  baro__c.num_asserts++;

  int pass =
    (type == BARO__ASSERT_EQ && lhs == rhs) ||
      (type == BARO__ASSERT_NE && lhs != rhs) ||
      (type == BARO__ASSERT_LT && lhs < rhs) ||
      (type == BARO__ASSERT_LE && lhs <= rhs) ||
      (type == BARO__ASSERT_GT && lhs > rhs) ||
      (type == BARO__ASSERT_GE && lhs >= rhs);

  if (!pass) {
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;

    baro__redirect_output(&baro__c, 0);

    char const *op =
      type == BARO__ASSERT_EQ ? "==" :
      type == BARO__ASSERT_NE ? "!=" :
      type == BARO__ASSERT_LT ? "<" :
      type == BARO__ASSERT_LE ? "<=" :
      type == BARO__ASSERT_GT ? ">" :
      type == BARO__ASSERT_GE ? ">=" : "";

    char const *assert_type = (required ? "Require" : "Check");
    printf("%s failed: %s\n", assert_type, desc);
    printf("    %s %s %s\n", lhs_str, op, rhs_str);
    printf("==> %zu %s %zu\n", lhs, op, rhs);
    printf("At %s:%d\n", file_name, line_num);

    baro__assert_failed(required);
  }
}

static inline void baro__assert_str(const char *lhs, const char *lhs_str, char const *rhs,
                                    char const *rhs_str, int equal, int case_sensitive, int required,
                                    char const *desc, char const *file_name, int line_num) {
  baro__c.num_asserts++;

  int pass =
    (case_sensitive && !!strcmp(lhs, rhs) == !equal) ||
    (!case_sensitive && !!strcasecmp(lhs, rhs) == !equal);

  if (!pass) {
    baro__c.current_test_failed = 1;
    baro__c.num_asserts_failed++;

    baro__redirect_output(&baro__c, 0);

    char const *op = (equal ? "==" : "!=");
    char const *assert_type = (required ? "Require" : "Check");
    char const *sensitivity = (case_sensitive ? "" : " (case insensitive)");

    char const *lhs_wrap = (lhs ? "\"" : ""), *rhs_wrap = (rhs ? "\"" : "");
    if (!lhs) {
      lhs = "[null]";
    }
    if (!rhs) {
      rhs = "[null]";
    }

    unsigned int str_len = strlen(lhs_str);
    unsigned int expanded_len = strlen(lhs) + strlen(lhs_wrap) * 2;

    unsigned int str_padding = 0, expanded_padding = 0;
    if (str_len > expanded_len) {
      expanded_padding = str_len - expanded_len;
    } else if (expanded_len > str_len) {
      str_padding = expanded_len - str_len;
    }

    printf("%s%s failed: %s\n", assert_type, sensitivity, desc);
    printf("    %s %*s%s %s\n", lhs_str, str_padding, "", op, rhs_str);
    printf("==> %s%s%s %*s%s %s%s%s\n", lhs_wrap, lhs, lhs_wrap, expanded_padding, "", op, rhs_wrap, rhs, rhs_wrap);
    printf("At %s:%d\n", file_name, line_num);

    baro__assert_failed(required);
  }
}

#define BARO__CONCAT(a, b) BARO__CONCAT_INTERNAL(a, b)
#define BARO__CONCAT_INTERNAL(a, b) a##b

#define BARO__WITH_COUNTER(x) BARO__CONCAT(x, __COUNTER__)

#ifdef _MSC_VER
#pragma section(".CRT$XCU", read)
#define BARO__INITIALIZER(f) \
  static void f(void); \
  __declspec(allocate(".CRT$XCU")) static void (*f##_init)(void) = f; \
  static void f(void)
#else
#define BARO__INITIALIZER(f) \
  __attribute__((constructor)) static void f(void)
#endif

#define BARO__CREATE_TEST_REGISTRAR(func_name, desc) \
  static struct baro__tag func_name##_tag = { desc, __FILE__, __LINE__ }; \
  BARO__INITIALIZER(func_name##_registrar) {              \
    baro__register_test(func_name, &func_name##_tag); \
  }

#ifdef BARO_ENABLE
#define BARO__TEST_FUNC(func_name, desc) \
  static void func_name(void); \
  BARO__CREATE_TEST_REGISTRAR(func_name, desc) \
  static void func_name(void)
#else
#define BARO__TEST_FUNC(func_name, ...) \
  static void func_name(void)
#endif

#define BARO_TEST(desc) \
  BARO__TEST_FUNC(BARO__WITH_COUNTER(BARO_TEST_), desc)

#ifdef BARO_ENABLE
#define BARO__SUBTEST_WRAPPER(desc, counter) \
  static struct baro__tag BARO__CONCAT(baro__subtest_tag_, counter) = { desc, __FILE__, __LINE__ }; \
  const int BARO__CONCAT(baro__enter_subtest_, counter) = baro__check_subtest(&BARO__CONCAT(baro__subtest_tag_, counter)); \
  if (BARO__CONCAT(baro__enter_subtest_, counter)) goto BARO__CONCAT(baro__subtest_, counter); \
  while (BARO__CONCAT(baro__enter_subtest_, counter)) if (1) { baro__exit_subtest(); break; } \
  else BARO__CONCAT(baro__subtest_, counter):
#else
#define BARO__SUBTEST_WRAPPER(...)
#endif

#define BARO_SUBTEST(desc) \
  BARO__SUBTEST_WRAPPER(desc, __COUNTER__)

#define BARO__CHECK1(cond) baro__assert1(cond, #cond, 1, 0, "", __FILE__, __LINE__)
#define BARO__CHECK2(cond, desc) baro__assert1(cond, #cond, 1, 0, desc, __FILE__, __LINE__)

#define BARO__REQUIRE1(cond) baro__assert1(cond, #cond, 1, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE2(cond, desc) baro__assert1(cond, #cond, 1, 1, desc, __FILE__, __LINE__)

#define BARO__CHECK_FALSE1(cond) baro__assert1(cond, #cond, 0, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_FALSE2(cond, desc) baro__assert1(cond, #cond, 0, 0, desc, __FILE__, __LINE__)

#define BARO__REQUIRE_FALSE1(cond) baro__assert1(cond, #cond, 0, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_FALSE2(cond, desc) baro__assert1(cond, #cond, 0, 1, desc, __FILE__, __LINE__)

#define BARO__CHECK_EQ1(lhs, rhs) baro__assert2(BARO__ASSERT_EQ, lhs, #lhs, rhs, #rhs, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_EQ2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_EQ, lhs, #lhs, rhs, #rhs, 0, desc, __FILE__, __LINE__)

#define BARO__REQUIRE_EQ1(lhs, rhs) baro__assert2(BARO__ASSERT_EQ, lhs, #lhs, rhs, #rhs, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_EQ2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_EQ, lhs, #lhs, rhs, #rhs, 1, desc, __FILE__, __LINE__)

#define BARO__CHECK_NE1(lhs, rhs) baro__assert2(BARO__ASSERT_NE, lhs, #lhs, rhs, #rhs, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_NE2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_NE, lhs, #lhs, rhs, #rhs, 0, desc, __FILE__, __LINE__)

#define BARO__REQUIRE_NE1(lhs, rhs) baro__assert2(BARO__ASSERT_NE, lhs, #lhs, rhs, #rhs, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_NE2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_NE, lhs, #lhs, rhs, #rhs, 1, desc, __FILE__, __LINE__)

#define BARO__CHECK_LT1(lhs, rhs) baro__assert2(BARO__ASSERT_LT, lhs, #lhs, rhs, #rhs, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_LT2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_LT, lhs, #lhs, rhs, #rhs, 0, desc, __FILE__, __LINE__)

#define BARO__REQUIRE_LT1(lhs, rhs) baro__assert2(BARO__ASSERT_LT, lhs, #lhs, rhs, #rhs, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_LT2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_LT, lhs, #lhs, rhs, #rhs, 1, desc, __FILE__, __LINE__)

#define BARO__CHECK_LE1(lhs, rhs) baro__assert2(BARO__ASSERT_LE, lhs, #lhs, rhs, #rhs, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_LE2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_LE, lhs, #lhs, rhs, #rhs, 0, desc, __FILE__, __LINE__)

#define BARO__REQUIRE_LE1(lhs, rhs) baro__assert2(BARO__ASSERT_LE, lhs, #lhs, rhs, #rhs, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_LE2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_LE, lhs, #lhs, rhs, #rhs, 1, desc, __FILE__, __LINE__)

#define BARO__CHECK_GT1(lhs, rhs) baro__assert2(BARO__ASSERT_GT, lhs, #lhs, rhs, #rhs, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_GT2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_GT, lhs, #lhs, rhs, #rhs, 0, desc, __FILE__, __LINE__)

#define BARO__REQUIRE_GT1(lhs, rhs) baro__assert2(BARO__ASSERT_GT, lhs, #lhs, rhs, #rhs, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_GT2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_GT, lhs, #lhs, rhs, #rhs, 1, desc, __FILE__, __LINE__)

#define BARO__CHECK_GE1(lhs, rhs) baro__assert2(BARO__ASSERT_GE, lhs, #lhs, rhs, #rhs, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_GE2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_GE, lhs, #lhs, rhs, #rhs, 0, desc, __FILE__, __LINE__)

#define BARO__REQUIRE_GE1(lhs, rhs) baro__assert2(BARO__ASSERT_GE, lhs, #lhs, rhs, #rhs, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_GE2(lhs, rhs, desc) baro__assert2(BARO__ASSERT_GE, lhs, #lhs, rhs, #rhs, 1, desc, __FILE__, __LINE__)

#define BARO__CHECK_STR_EQ2(lhs, rhs) baro__assert_str(lhs, #lhs, rhs, #rhs, 1, 1, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_STR_EQ3(lhs, rhs, desc) baro__assert_str(lhs, #lhs, rhs, #rhs, 1, 1, 0, "", __FILE__, __LINE__)

#define BARO__REQUIRE_STR_EQ2(lhs, rhs) baro__assert_str(lhs, #lhs, rhs, #rhs, 1, 1, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_STR_EQ3(lhs, rhs, desc) baro__assert_str(lhs, #lhs, rhs, #rhs, 1, 1, 1, "", __FILE__, __LINE__)

#define BARO__CHECK_STR_NE2(lhs, rhs) baro__assert_str(lhs, #lhs, rhs, #rhs, 0, 1, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_STR_NE3(lhs, rhs, desc) baro__assert_str(lhs, #lhs, rhs, #rhs, 0, 1, 0, "", __FILE__, __LINE__)

#define BARO__REQUIRE_STR_NE2(lhs, rhs) baro__assert_str(lhs, #lhs, rhs, #rhs, 0, 1, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_STR_NE3(lhs, rhs, desc) baro__assert_str(lhs, #lhs, rhs, #rhs, 0, 1, 1, "", __FILE__, __LINE__)

#define BARO__CHECK_STR_ICASE_EQ2(lhs, rhs) baro__assert_str(lhs, #lhs, rhs, #rhs, 1, 0, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_STR_ICASE_EQ3(lhs, rhs, desc) baro__assert_str(lhs, #lhs, rhs, #rhs, 1, 0, 0, "", __FILE__, __LINE__)

#define BARO__REQUIRE_STR_ICASE_EQ2(lhs, rhs) baro__assert_str(lhs, #lhs, rhs, #rhs, 1, 0, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_STR_ICASE_EQ3(lhs, rhs, desc) baro__assert_str(lhs, #lhs, rhs, #rhs, 1, 0, 1, "", __FILE__, __LINE__)

#define BARO__CHECK_STR_ICASE_NE2(lhs, rhs) baro__assert_str(lhs, #lhs, rhs, #rhs, 0, 0, 0, "", __FILE__, __LINE__)
#define BARO__CHECK_STR_ICASE_NE3(lhs, rhs, desc) baro__assert_str(lhs, #lhs, rhs, #rhs, 0, 0, 0, "", __FILE__, __LINE__)

#define BARO__REQUIRE_STR_ICASE_NE2(lhs, rhs) baro__assert_str(lhs, #lhs, rhs, #rhs, 0, 0, 1, "", __FILE__, __LINE__)
#define BARO__REQUIRE_STR_ICASE_NE3(lhs, rhs, desc) baro__assert_str(lhs, #lhs, rhs, #rhs, 0, 0, 1, "", __FILE__, __LINE__)

#define BARO__GET2(_1, _2, NAME, ...) NAME
#define BARO__GET3(_1, _2, _3, NAME, ...) NAME

#ifdef _WIN32
#define BARO__X(x) x
#define BARO_CHECK(...) BARO__X(BARO__GET2(__VA_ARGS__, BARO__CHECK2, BARO__CHECK1))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE(...) BARO__X(BARO__GET2(__VA_ARGS__, BARO__REQUIRE2, BARO__REQUIRE1))BARO__X((__VA_ARGS__))
#define BARO_CHECK_FALSE(...) BARO__X(BARO__GET2(__VA_ARGS__, BARO__CHECK_FALSE2, BARO__CHECK_FALSE1))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_FALSE(...) BARO__X(BARO__GET2(__VA_ARGS__, BARO__REQUIRE_FALSE2, BARO__REQUIRE_FALSE1))BARO__X((__VA_ARGS__))
#define BARO_CHECK_EQ(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_EQ2, BARO__CHECK_EQ1,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_EQ(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_EQ2, BARO__REQUIRE_EQ1,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_NE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_NE2, BARO__CHECK_NE1,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_NE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_NE2, BARO__REQUIRE_NE1,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_LT(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_LT2, BARO__CHECK_LT1,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_LT(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_LT2, BARO__REQUIRE_LT1,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_LE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_LE2, BARO__CHECK_LE1,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_LE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_LE2, BARO__REQUIRE_LE1,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_GT(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_GT2, BARO__CHECK_GT1,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_GT(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_GT2, BARO__REQUIRE_GT1,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_GE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_GE2, BARO__CHECK_GE1,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_GE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_GE2, BARO__REQUIRE_GE1,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_STR_EQ(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_STR_EQ3, BARO__CHECK_STR_EQ2,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_STR_EQ(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_STR_EQ3, BARO__REQUIRE_STR_EQ2,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_STR_NE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_STR_NE3, BARO__CHECK_STR_NE2,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_STR_NE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_STR_NE3, BARO__REQUIRE_STR_NE2,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_STR_ICASE_EQ(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_STR_ICASE_EQ3, BARO__CHECK_STR_ICASE_EQ2,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_STR_ICASE_EQ(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_STR_ICASE_EQ3, BARO__REQUIRE_STR_ICASE_EQ2,))BARO__X((__VA_ARGS__))
#define BARO_CHECK_STR_ICASE_NE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__CHECK_STR_ICASE_NE3, BARO__CHECK_STR_ICASE_NE2,))BARO__X((__VA_ARGS__))
#define BARO_REQUIRE_STR_ICASE_NE(...) BARO__X(BARO__GET3(__VA_ARGS__, BARO__REQUIRE_STR_ICASE_NE3, BARO__REQUIRE_STR_ICASE_NE2,))BARO__X((__VA_ARGS__))
#else
#define BARO_CHECK(...) BARO__GET2(__VA_ARGS__, BARO__CHECK2, BARO__CHECK1)(__VA_ARGS__)
#define BARO_REQUIRE(...) BARO__GET2(__VA_ARGS__, BARO__REQUIRE2, BARO__REQUIRE1)(__VA_ARGS__)
#define BARO_CHECK_FALSE(...) BARO__GET2(__VA_ARGS__, BARO__CHECK_FALSE2, BARO__CHECK_FALSE1)(__VA_ARGS__)
#define BARO_REQUIRE_FALSE(...) BARO__GET2(__VA_ARGS__, BARO__REQUIRE_FALSE2, BARO__REQUIRE_FALSE1)(__VA_ARGS__)
#define BARO_CHECK_EQ(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_EQ2, BARO__CHECK_EQ1,)(__VA_ARGS__)
#define BARO_REQUIRE_EQ(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_EQ2, BARO__REQUIRE_EQ1,)(__VA_ARGS__)
#define BARO_CHECK_NE(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_NE2, BARO__CHECK_NE1,)(__VA_ARGS__)
#define BARO_REQUIRE_NE(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_NE2, BARO__REQUIRE_NE1,)(__VA_ARGS__)
#define BARO_CHECK_LT(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_LT2, BARO__CHECK_LT1,)(__VA_ARGS__)
#define BARO_REQUIRE_LT(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_LT2, BARO__REQUIRE_LT1,)(__VA_ARGS__)
#define BARO_CHECK_LE(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_LE2, BARO__CHECK_LE1,)(__VA_ARGS__)
#define BARO_REQUIRE_LE(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_LE2, BARO__REQUIRE_LE1,)(__VA_ARGS__)
#define BARO_CHECK_GT(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_GT2, BARO__CHECK_GT1,)(__VA_ARGS__)
#define BARO_REQUIRE_GT(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_GT2, BARO__REQUIRE_GT1,)(__VA_ARGS__)
#define BARO_CHECK_GE(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_GE2, BARO__CHECK_GE1,)(__VA_ARGS__)
#define BARO_REQUIRE_GE(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_GE2, BARO__REQUIRE_GE1,)(__VA_ARGS__)
#define BARO_CHECK_STR_EQ(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_STR_EQ3, BARO__CHECK_STR_EQ2,)(__VA_ARGS__)
#define BARO_REQUIRE_STR_EQ(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_STR_EQ3, BARO__REQUIRE_STR_EQ2,)(__VA_ARGS__)
#define BARO_CHECK_STR_NE(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_STR_NE3, BARO__CHECK_STR_NE2,)(__VA_ARGS__)
#define BARO_REQUIRE_STR_NE(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_STR_NE3, BARO__REQUIRE_STR_NE2,)(__VA_ARGS__)
#define BARO_CHECK_STR_ICASE_EQ(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_STR_ICASE_EQ3, BARO__CHECK_STR_ICASE_EQ2,)(__VA_ARGS__)
#define BARO_REQUIRE_STR_ICASE_EQ(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_STR_ICASE_EQ3, BARO__REQUIRE_STR_ICASE_EQ2,)(__VA_ARGS__)
#define BARO_CHECK_STR_ICASE_NE(...) BARO__GET3(__VA_ARGS__, BARO__CHECK_STR_ICASE_NE3, BARO__CHECK_STR_ICASE_NE2,)(__VA_ARGS__)
#define BARO_REQUIRE_STR_ICASE_NE(...) BARO__GET3(__VA_ARGS__, BARO__REQUIRE_STR_ICASE_NE3, BARO__REQUIRE_STR_ICASE_NE2,)(__VA_ARGS__)
#endif

#ifndef BARO_NO_SHORT
#define TEST BARO_TEST
#define SUBTEST BARO_SUBTEST
#define CHECK BARO_CHECK
#define REQUIRE BARO_REQUIRE
#define CHECK_FALSE BARO_CHECK_FALSE
#define REQUIRE_FALSE BARO_REQUIRE_FALSE
#define CHECK_EQ BARO_CHECK_EQ
#define REQUIRE_EQ BARO_REQUIRE_EQ
#define CHECK_NE BARO_CHECK_NE
#define REQUIRE_NE BARO_REQUIRE_NE
#define CHECK_LT BARO_CHECK_LT
#define REQUIRE_LT BARO_REQUIRE_LT
#define CHECK_LE BARO_CHECK_LE
#define REQUIRE_LE BARO_REQUIRE_LE
#define CHECK_GT BARO_CHECK_GT
#define REQUIRE_GT BARO_REQUIRE_GT
#define CHECK_GE BARO_CHECK_GE
#define REQUIRE_GE BARO_REQUIRE_GE
#define CHECK_STR_EQ BARO_CHECK_STR_EQ
#define REQUIRE_STR_EQ BARO_CHECK_STR_EQ
#define CHECK_STR_NE BARO_CHECK_STR_NE
#define REQUIRE_STR_NE BARO_REQUIRE_STR_NE
#define CHECK_STR_ICASE_EQ BARO_CHECK_STR_ICASE_EQ
#define REQUIRE_STR_ICASE_EQ BARO_REQUIRE_STR_ICASE_EQ
#define CHECK_STR_ICASE_NE BARO_CHECK_STR_ICASE_NE
#define REQUIRE_STR_ICASE_NE BARO_REQUIRE_STR_ICASE_NE
#endif

#ifdef BARO_MAIN
int baro__init = 0;
struct baro__context baro__c;

int main(int argc, char *argv[]) {
  size_t num_tests = baro__c.tests.size;
  printf("Running %zu test%s\n", num_tests, num_tests > 1 ? "s" : "");

  printf(BARO__SEPARATOR);

  baro__redirect_output(&baro__c, 1);

  for (size_t i = 0; i < num_tests; i++) {
    struct baro__test *test = &baro__c.tests.tests[i];
    baro__c.current_test = test;
    baro__c.current_test_failed = 0;

    int run_test = 1;

    if (setjmp(baro__c.env)) {
      run_test = 0;
    }

    while (run_test) {
      baro__c.should_reenter_subtest = 0;
      baro__c.subtest_max_size = 0;
      baro__tag_list_clear(&baro__c.subtest_stack);

      test->func();

      if (!baro__c.should_reenter_subtest) {
        run_test = 0;
      }
    }

    baro__c.num_tests++;
    if (baro__c.current_test_failed) {
      baro__c.num_tests_failed++;
    }
  }

  baro__redirect_output(&baro__c, 0);

  printf("tests:   %5zu total | %5zu passed | %5zu failed\n",
         baro__c.num_tests, baro__c.num_tests - baro__c.num_tests_failed,
         baro__c.num_tests_failed);

  printf("asserts: %5zu total | %5zu passed | %5zu failed\n",
         baro__c.num_asserts, baro__c.num_asserts - baro__c.num_asserts_failed,
         baro__c.num_asserts_failed);

  return baro__c.num_tests_failed;
}
#endif
#endif //BARO_3FDC036FA2C64C72A0DB6BA1033C678B
