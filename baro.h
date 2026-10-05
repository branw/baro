#ifndef BARO_3FDC036FA2C64C72A0DB6BA1033C678B
#define BARO_3FDC036FA2C64C72A0DB6BA1033C678B

#include <stddef.h>
#include <stdint.h>

// Call once per process. Custom runners dispatch child/discovery modes before parent setup.
int baro_run(int argc, char *argv[]);
int baro_is_child(int argc, char *argv[]);
int baro_is_discovery(int argc, char *argv[]);

#ifdef BARO_ENABLE

// Everything named baro__ is the interface between the macros below and the
// runtime in baro.c. It is not a public API and may change between versions.
struct baro__tag {
    char const *desc;
    char const *file_path;
    int line_num;
};

// One SUBTEST statement. It remembers the parent path under which all of its
// leaves last finished, so later traversals skip it without a set lookup.
struct baro__subtest {
    struct baro__tag tag;
    uint64_t done_parent;
    size_t done_run;
};

enum baro__assert_type {
    BARO__ASSERT_CHECK,
    BARO__ASSERT_REQUIRE,
};

enum baro__assert_cond {
    BARO__ASSERT_EQ,
    BARO__ASSERT_NE,
    BARO__ASSERT_LT,
    BARO__ASSERT_LE,
    BARO__ASSERT_GT,
    BARO__ASSERT_GE,
};

enum baro__case_sensitivity {
    BARO__CASE_SENSITIVE,
    BARO__CASE_INSENSITIVE,
};

enum baro__expected_value {
    BARO__EXPECTING_FALSE,
    BARO__EXPECTING_TRUE,
};

void baro__register_test_kind(void (*test_func)(void), struct baro__tag const *tag, int expect_abort);
int baro__check_subtest(struct baro__subtest *site);
void baro__exit_subtest(struct baro__subtest *site);

// The payload is copied, so it survives REQUIRE unwinding the test's stack.
// Payloads containing pointers must refer to heap or static storage.
void baro_defer(void (*callback)(void *), const void *data, size_t size);

void baro__assert1(size_t value, char const *value_str, enum baro__expected_value expected_value,
                   enum baro__assert_type type, char const *desc, char const *file_path, int line_num);
void baro__assert2(enum baro__assert_cond cond, int passed, char const *lhs_str, char const *rhs_str,
                   enum baro__assert_type type, char const *desc, char const *file_path, int line_num);
void baro__assert_str(char const *lhs, char const *lhs_str, char const *rhs, char const *rhs_str,
                      enum baro__expected_value expected_value,
                      enum baro__case_sensitivity case_sensitivity, enum baro__assert_type type,
                      char const *desc, char const *file_path, int line_num);
void baro__assert_arr(uint8_t const *lhs, char const *lhs_str, uint8_t const *rhs, char const *rhs_str,
                      size_t element_size, size_t element_count,
                      enum baro__expected_value expected_value, enum baro__assert_type type,
                      char const *desc, char const *file_path, int line_num);
void baro__typed_int(enum baro__assert_cond cond, intmax_t lhs, const char *lhs_str,
                     intmax_t rhs, const char *rhs_str, int hard, const char *file, int line);
void baro__typed_uint(enum baro__assert_cond cond, uintmax_t lhs, const char *lhs_str,
                      uintmax_t rhs, const char *rhs_str, int hard, const char *file, int line);
void baro__typed_ptr(enum baro__assert_cond cond, const void *lhs, const char *lhs_str,
                     const void *rhs, const char *rhs_str, int hard, const char *file, int line);
void baro__typed_double(enum baro__assert_cond cond, double lhs, const char *lhs_str,
                        double rhs, const char *rhs_str, int hard, const char *file, int line);

// How baro__assert_values reads the operands captured by a generic comparison.
enum baro__value_kind {
    BARO__VALUE_NONE,
    BARO__VALUE_INT,
    BARO__VALUE_UINT,
    BARO__VALUE_LONG,
    BARO__VALUE_ULONG,
    BARO__VALUE_LLONG,
    BARO__VALUE_ULLONG,
    BARO__VALUE_FLOAT,
    BARO__VALUE_DOUBLE,
    BARO__VALUE_LDOUBLE,
    // A pointer, or a scalar type with no formatter, which is not displayed.
    BARO__VALUE_OTHER,
};
void baro__assert_values(enum baro__assert_cond cond, int passed, enum baro__value_kind kind,
                         const void *lhs, const void *rhs, int pointer,
                         const char *lhs_str, const char *rhs_str, enum baro__assert_type type,
                         const char *desc, const char *file, int line);
void baro__near(double lhs, double rhs, double absolute, double relative,
                int hard, const char *file, int line);
// Turn the regular assert.h assert() into a baro assertion. This is a
// best-effort mechanism that only works in files that include <baro.h> (after
// including <assert.h>).
#ifdef BARO_REPLACE_ASSERT
#undef assert
#define assert(e) BARO_REQUIRE(e, "Assertion failed (" #e ")")
#endif

#else
static inline void baro_defer(void (*callback)(void *), const void *data, size_t size) {
    (void)callback; (void)data; (void)size;
}
#ifndef assert
#include <assert.h>
#endif//!defined(assert)
#endif//BARO_ENABLE

#define BARO__CONCAT(a, b) BARO__CONCAT_INTERNAL(a, b)
#define BARO__CONCAT_INTERNAL(a, b) a##b

#define BARO__WITH_COUNTER(x) BARO__CONCAT(x, __COUNTER__)

#ifdef _MSC_VER
// MSVC doesn't support the constructor attribute. Instead, we can place our
// function in one of the CRT initializer segments, so that it still gets
// called before `main`. The `C` in `XCU` indicates it will run with the other
// C++ initializers (and after C initializers), while the `U` puts it after any
// MSVC-generated initializers, like for globals. See the CRT's `defects.inc`
// for "documentation" of this behavior.
#pragma section(".CRT$XCU", read)
#define BARO__INITIALIZER(f)                                                   \
    static void f(void);                                                       \
    __declspec(allocate(".CRT$XCU")) static void (* const f##_init)(void) = f; \
    static void f(void)
#else
#define BARO__INITIALIZER(f) \
    __attribute__((constructor)) static void f(void)
#endif//_MSC_VER

// All test functions are registered by a "registrar function" sometime during
// runtime initialization. This is used to automatically build a list of all
// tests, across compilation units, for the test runner.
#define BARO__CREATE_TEST_REGISTRAR(func_name, desc, expected)                            \
    static struct baro__tag const func_name##_tag = {desc, __FILE__, __LINE__}; \
    BARO__INITIALIZER(func_name##_registrar) {                                  \
        baro__register_test_kind(func_name, &func_name##_tag, expected);                       \
    }

#ifdef BARO_ENABLE
#define BARO__TEST_FUNC(func_name, desc, expected)         \
    static void func_name(void);                 \
    BARO__CREATE_TEST_REGISTRAR(func_name, desc, expected) \
    static void func_name(void)
#elif defined(_MSC_VER)
#define BARO__TEST_FUNC(func_name, ...) static void func_name(void)
#else
#define BARO__TEST_FUNC(func_name, ...) \
    static void __attribute__((unused)) func_name(void)
#endif//BARO_ENABLE

#define BARO_TEST_ABORT(desc) BARO__TEST_FUNC(BARO__WITH_COUNTER(BARO_TEST_), desc, 1)
#define BARO_TEST(desc) BARO__TEST_FUNC(BARO__WITH_COUNTER(BARO_TEST_), desc, 0)

#ifdef BARO_ENABLE
// Here we abuse a while loop so that our macro can call functions before and
// after any arbitrary block of code. This allows us to check if a subtest
// should be executed (i.e. if we haven't exhausted all combinations including
// it), while also updating the stack once we leave the subtest.
#define BARO__SUBTEST_WRAPPER(desc, counter)                                                                                 \
    static struct baro__subtest BARO__CONCAT(baro__subtest_site_, counter) = {{desc, __FILE__, __LINE__}, 0, 0};             \
    int const BARO__CONCAT(baro__enter_subtest_, counter) = baro__check_subtest(&BARO__CONCAT(baro__subtest_site_, counter));\
    if (BARO__CONCAT(baro__enter_subtest_, counter)) goto BARO__CONCAT(baro__subtest_, counter);                             \
    while (BARO__CONCAT(baro__enter_subtest_, counter))                                                                      \
        if (1) {                                                                                                             \
            baro__exit_subtest(&BARO__CONCAT(baro__subtest_site_, counter));                                                 \
            break;                                                                                                           \
        } else                                                                                                               \
            BARO__CONCAT(baro__subtest_, counter) :
#else
#define BARO__SUBTEST_WRAPPER(...)
#endif//BARO_ENABLE

#if __STDC_VERSION__ < 201112L
#define BARO__STATIC_ASSERT(X, Y) ((void)sizeof(char[(X) ? 1 : -1]))
#else
#define BARO__STATIC_ASSERT(X, Y) _Static_assert(X, Y)
#endif

#define BARO_SUBTEST(desc) BARO__SUBTEST_WRAPPER(desc, __COUNTER__)

// Assertions take an optional trailing description, which must be a string
// literal. Each public macro appends two placeholders to its arguments, so the
// family macro it forwards to always finds a description, and the argument
// after that is a placeholder unless too many arguments were given. BARO__EXPAND
// makes MSVC's traditional preprocessor split the forwarded arguments.
#define BARO__EXPAND(x) x
#define BARO__OMITTED ""
#define BARO__END_BARO__OMITTED

#ifdef BARO_ENABLE
#define BARO__TRUTH(expected, type, cond, desc, end, ...)                                           \
    baro__assert1(((cond) != 0), #cond, expected, type, "" desc, __FILE__, __LINE__) BARO__END_##end
#define BARO__STRING(expected, sensitivity, type, lhs, rhs, desc, end, ...)                         \
    baro__assert_str(lhs, #lhs, rhs, #rhs, expected, sensitivity, type, "" desc,                    \
                     __FILE__, __LINE__) BARO__END_##end
#define BARO__ARRAY(expected, type, lhs, rhs, count, desc, end, ...) do {                           \
    BARO__STATIC_ASSERT(sizeof((lhs)[0]) == sizeof((rhs)[0]), "Mismatched array types");            \
    baro__assert_arr((uint8_t const *) (lhs), #lhs, (uint8_t const *) (rhs), #rhs,                  \
                     sizeof((lhs)[0]), count, expected, type, "" desc, __FILE__, __LINE__);         \
} while (0) BARO__END_##end
#else
#define BARO__TRUTH(expected, type, cond, desc, end, ...)                                           \
    do { (void)(cond); (void)("" desc); } while (0) BARO__END_##end
#define BARO__STRING(expected, sensitivity, type, lhs, rhs, desc, end, ...)                         \
    do { (void)(lhs); (void)(rhs); (void)("" desc); } while (0) BARO__END_##end
#define BARO__ARRAY(expected, type, lhs, rhs, count, desc, end, ...)                                \
    do { (void)(lhs); (void)(rhs); (void)sizeof((lhs)[0]); (void)(count); (void)("" desc); }        \
    while (0) BARO__END_##end
#endif//BARO_ENABLE

// Generic comparisons also display both values when the compiler can capture
// each operand once and select a formatter by type (C11 _Generic). Both
// operands are captured in the type C itself converts them to for the
// comparison, which is the type of a conditional expression over the two, so
// the result is unchanged. C99 mode keeps the expression-only diagnostic.
#if defined(BARO_ENABLE) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 5)
// Unlike typeof, an inferred type never evaluates its initializer twice, even
// for the variably modified types these compilers support.
#define BARO__CAPTURE_LHS(lhs, rhs) __extension__ __auto_type baro__lhs = 1 ? (lhs) : (rhs)
#define BARO__CAPTURE_RHS(lhs, rhs) __extension__ __auto_type baro__rhs = 0 ? (lhs) : (rhs)
#define BARO__IS_POINTER(x) (__builtin_classify_type(x) == 5)
#elif __STDC_VERSION__ >= 202311L
#define BARO__CAPTURE_LHS(lhs, rhs) auto baro__lhs = 1 ? (lhs) : (rhs)
#define BARO__CAPTURE_RHS(lhs, rhs) auto baro__rhs = 0 ? (lhs) : (rhs)
#elif defined(_MSC_VER) && _MSC_VER >= 1939
#define BARO__CAPTURE_LHS(lhs, rhs) __typeof__(0 ? (lhs) : (rhs)) baro__lhs = (lhs)
#define BARO__CAPTURE_RHS(lhs, rhs) __typeof__(baro__lhs) baro__rhs = (rhs)
#endif
// Without a type query, assume that an unlisted scalar of pointer size is one.
#if defined(BARO__CAPTURE_LHS) && !defined(BARO__IS_POINTER)
#define BARO__IS_POINTER(x) (sizeof(x) == sizeof(void *))
#endif
#endif

#if !defined(BARO_ENABLE)
#define BARO__COMPARE(cond, op, type, lhs, rhs, desc, end, ...)                                     \
    do { (void)((lhs) op (rhs)); (void)("" desc); } while (0) BARO__END_##end
#elif defined(BARO__CAPTURE_LHS)
// Operands are promoted before they are compared, so narrower types never occur.
#define BARO__VALUE_KIND(x) _Generic((x),                                             \
    int: BARO__VALUE_INT, unsigned: BARO__VALUE_UINT,                                 \
    long: BARO__VALUE_LONG, unsigned long: BARO__VALUE_ULONG,                         \
    long long: BARO__VALUE_LLONG, unsigned long long: BARO__VALUE_ULLONG,             \
    float: BARO__VALUE_FLOAT, double: BARO__VALUE_DOUBLE,                             \
    long double: BARO__VALUE_LDOUBLE, default: BARO__VALUE_OTHER)
#define BARO__COMPARE(cond, op, type, lhs, rhs, desc, end, ...) do {                                \
    BARO__CAPTURE_LHS(lhs, rhs);                                                                    \
    BARO__CAPTURE_RHS(lhs, rhs);                                                                    \
    baro__assert_values(cond, baro__lhs op baro__rhs, BARO__VALUE_KIND(baro__lhs),                  \
                        &baro__lhs, &baro__rhs, BARO__IS_POINTER(baro__lhs),                        \
                        #lhs, #rhs, type, "" desc, __FILE__, __LINE__);                             \
} while (0) BARO__END_##end
#else
#define BARO__COMPARE(cond, op, type, lhs, rhs, desc, end, ...) do {                                \
    baro__assert2(cond, ((lhs) op (rhs)), #lhs, #rhs, type, "" desc, __FILE__, __LINE__);           \
} while (0) BARO__END_##end
#endif

#define BARO_CHECK(...) BARO__EXPAND(BARO__TRUTH(BARO__EXPECTING_TRUE, BARO__ASSERT_CHECK,          \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE(...) BARO__EXPAND(BARO__TRUTH(BARO__EXPECTING_TRUE, BARO__ASSERT_REQUIRE,      \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_FALSE(...) BARO__EXPAND(BARO__TRUTH(BARO__EXPECTING_FALSE, BARO__ASSERT_CHECK,   \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_FALSE(...) BARO__EXPAND(BARO__TRUTH(BARO__EXPECTING_FALSE, BARO__ASSERT_REQUIRE,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_EQ(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_EQ, ==, BARO__ASSERT_CHECK,      \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_EQ(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_EQ, ==, BARO__ASSERT_REQUIRE,  \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_NE(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_NE, !=, BARO__ASSERT_CHECK,      \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_NE(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_NE, !=, BARO__ASSERT_REQUIRE,  \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_LT(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_LT, <, BARO__ASSERT_CHECK,       \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_LT(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_LT, <, BARO__ASSERT_REQUIRE,   \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_LE(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_LE, <=, BARO__ASSERT_CHECK,      \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_LE(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_LE, <=, BARO__ASSERT_REQUIRE,  \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_GT(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_GT, >, BARO__ASSERT_CHECK,       \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_GT(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_GT, >, BARO__ASSERT_REQUIRE,   \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_GE(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_GE, >=, BARO__ASSERT_CHECK,      \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_GE(...) BARO__EXPAND(BARO__COMPARE(BARO__ASSERT_GE, >=, BARO__ASSERT_REQUIRE,  \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_STR_EQ(...) BARO__EXPAND(BARO__STRING(BARO__EXPECTING_TRUE, BARO__CASE_SENSITIVE, BARO__ASSERT_CHECK,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_STR_EQ(...) BARO__EXPAND(BARO__STRING(BARO__EXPECTING_TRUE, BARO__CASE_SENSITIVE, BARO__ASSERT_REQUIRE,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_STR_NE(...) BARO__EXPAND(BARO__STRING(BARO__EXPECTING_FALSE, BARO__CASE_SENSITIVE, BARO__ASSERT_CHECK,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_STR_NE(...) BARO__EXPAND(BARO__STRING(BARO__EXPECTING_FALSE, BARO__CASE_SENSITIVE, BARO__ASSERT_REQUIRE,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_STR_ICASE_EQ(...) BARO__EXPAND(BARO__STRING(BARO__EXPECTING_TRUE, BARO__CASE_INSENSITIVE, BARO__ASSERT_CHECK,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_STR_ICASE_EQ(...) BARO__EXPAND(BARO__STRING(BARO__EXPECTING_TRUE, BARO__CASE_INSENSITIVE, BARO__ASSERT_REQUIRE,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_STR_ICASE_NE(...) BARO__EXPAND(BARO__STRING(BARO__EXPECTING_FALSE, BARO__CASE_INSENSITIVE, BARO__ASSERT_CHECK,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_STR_ICASE_NE(...) BARO__EXPAND(BARO__STRING(BARO__EXPECTING_FALSE, BARO__CASE_INSENSITIVE, BARO__ASSERT_REQUIRE,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_ARR_EQ(...) BARO__EXPAND(BARO__ARRAY(BARO__EXPECTING_TRUE, BARO__ASSERT_CHECK,   \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_ARR_EQ(...) BARO__EXPAND(BARO__ARRAY(BARO__EXPECTING_TRUE, BARO__ASSERT_REQUIRE,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_CHECK_ARR_NE(...) BARO__EXPAND(BARO__ARRAY(BARO__EXPECTING_FALSE, BARO__ASSERT_CHECK,  \
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))
#define BARO_REQUIRE_ARR_NE(...) BARO__EXPAND(BARO__ARRAY(BARO__EXPECTING_FALSE, BARO__ASSERT_REQUIRE,\
    __VA_ARGS__, BARO__OMITTED, BARO__OMITTED, 0))

// Typed assertions deliberately convert operands to the named type.
#ifdef BARO_ENABLE
#define BARO__TYPED(kind, cond, a, a_str, b, b_str, hard) \
    baro__typed_##kind(BARO__ASSERT_##cond, (a), a_str, (b), b_str, hard, __FILE__, __LINE__)
#define BARO__NEAR(a, b, abs_tol, rel_tol, hard) baro__near((a), (b), (abs_tol), (rel_tol), hard, __FILE__, __LINE__)
#else
#define BARO__TYPED(kind, cond, a, a_str, b, b_str, hard) do { (void)(a); (void)(b); } while (0)
#define BARO__NEAR(a, b, abs_tol, rel_tol, hard) do { (void)(a); (void)(b); (void)(abs_tol); (void)(rel_tol); } while (0)
#endif
#define BARO_CHECK_INT_EQ(a, b) BARO__TYPED(int, EQ, a, #a, b, #b, 0)
#define BARO_REQUIRE_INT_EQ(a, b) BARO__TYPED(int, EQ, a, #a, b, #b, 1)
#define BARO_CHECK_INT_NE(a, b) BARO__TYPED(int, NE, a, #a, b, #b, 0)
#define BARO_REQUIRE_INT_NE(a, b) BARO__TYPED(int, NE, a, #a, b, #b, 1)
#define BARO_CHECK_INT_LT(a, b) BARO__TYPED(int, LT, a, #a, b, #b, 0)
#define BARO_REQUIRE_INT_LT(a, b) BARO__TYPED(int, LT, a, #a, b, #b, 1)
#define BARO_CHECK_INT_LE(a, b) BARO__TYPED(int, LE, a, #a, b, #b, 0)
#define BARO_REQUIRE_INT_LE(a, b) BARO__TYPED(int, LE, a, #a, b, #b, 1)
#define BARO_CHECK_INT_GT(a, b) BARO__TYPED(int, GT, a, #a, b, #b, 0)
#define BARO_REQUIRE_INT_GT(a, b) BARO__TYPED(int, GT, a, #a, b, #b, 1)
#define BARO_CHECK_INT_GE(a, b) BARO__TYPED(int, GE, a, #a, b, #b, 0)
#define BARO_REQUIRE_INT_GE(a, b) BARO__TYPED(int, GE, a, #a, b, #b, 1)
#define BARO_CHECK_UINT_EQ(a, b) BARO__TYPED(uint, EQ, a, #a, b, #b, 0)
#define BARO_REQUIRE_UINT_EQ(a, b) BARO__TYPED(uint, EQ, a, #a, b, #b, 1)
#define BARO_CHECK_UINT_NE(a, b) BARO__TYPED(uint, NE, a, #a, b, #b, 0)
#define BARO_REQUIRE_UINT_NE(a, b) BARO__TYPED(uint, NE, a, #a, b, #b, 1)
#define BARO_CHECK_UINT_LT(a, b) BARO__TYPED(uint, LT, a, #a, b, #b, 0)
#define BARO_REQUIRE_UINT_LT(a, b) BARO__TYPED(uint, LT, a, #a, b, #b, 1)
#define BARO_CHECK_UINT_LE(a, b) BARO__TYPED(uint, LE, a, #a, b, #b, 0)
#define BARO_REQUIRE_UINT_LE(a, b) BARO__TYPED(uint, LE, a, #a, b, #b, 1)
#define BARO_CHECK_UINT_GT(a, b) BARO__TYPED(uint, GT, a, #a, b, #b, 0)
#define BARO_REQUIRE_UINT_GT(a, b) BARO__TYPED(uint, GT, a, #a, b, #b, 1)
#define BARO_CHECK_UINT_GE(a, b) BARO__TYPED(uint, GE, a, #a, b, #b, 0)
#define BARO_REQUIRE_UINT_GE(a, b) BARO__TYPED(uint, GE, a, #a, b, #b, 1)
#define BARO_CHECK_PTR_EQ(a, b) BARO__TYPED(ptr, EQ, a, #a, b, #b, 0)
#define BARO_REQUIRE_PTR_EQ(a, b) BARO__TYPED(ptr, EQ, a, #a, b, #b, 1)
#define BARO_CHECK_PTR_NE(a, b) BARO__TYPED(ptr, NE, a, #a, b, #b, 0)
#define BARO_REQUIRE_PTR_NE(a, b) BARO__TYPED(ptr, NE, a, #a, b, #b, 1)
#define BARO_CHECK_DOUBLE_EQ(a, b) BARO__TYPED(double, EQ, a, #a, b, #b, 0)
#define BARO_REQUIRE_DOUBLE_EQ(a, b) BARO__TYPED(double, EQ, a, #a, b, #b, 1)
#define BARO_CHECK_DOUBLE_NE(a, b) BARO__TYPED(double, NE, a, #a, b, #b, 0)
#define BARO_REQUIRE_DOUBLE_NE(a, b) BARO__TYPED(double, NE, a, #a, b, #b, 1)
#define BARO_CHECK_DOUBLE_LT(a, b) BARO__TYPED(double, LT, a, #a, b, #b, 0)
#define BARO_REQUIRE_DOUBLE_LT(a, b) BARO__TYPED(double, LT, a, #a, b, #b, 1)
#define BARO_CHECK_DOUBLE_LE(a, b) BARO__TYPED(double, LE, a, #a, b, #b, 0)
#define BARO_REQUIRE_DOUBLE_LE(a, b) BARO__TYPED(double, LE, a, #a, b, #b, 1)
#define BARO_CHECK_DOUBLE_GT(a, b) BARO__TYPED(double, GT, a, #a, b, #b, 0)
#define BARO_REQUIRE_DOUBLE_GT(a, b) BARO__TYPED(double, GT, a, #a, b, #b, 1)
#define BARO_CHECK_DOUBLE_GE(a, b) BARO__TYPED(double, GE, a, #a, b, #b, 0)
#define BARO_REQUIRE_DOUBLE_GE(a, b) BARO__TYPED(double, GE, a, #a, b, #b, 1)
#define BARO_CHECK_NEAR(a, b, abs_tol, rel_tol) BARO__NEAR(a, b, abs_tol, rel_tol, 0)
#define BARO_REQUIRE_NEAR(a, b, abs_tol, rel_tol) BARO__NEAR(a, b, abs_tol, rel_tol, 1)
// ARR compares object representations; BYTES makes that intent explicit.
#define BARO_CHECK_BYTES_EQ(a, b, n) BARO_CHECK_ARR_EQ((const uint8_t *)(a), (const uint8_t *)(b), n)
#define BARO_REQUIRE_BYTES_EQ(a, b, n) BARO_REQUIRE_ARR_EQ((const uint8_t *)(a), (const uint8_t *)(b), n)
#ifndef BARO_NO_SHORT
#define CHECK_INT_EQ BARO_CHECK_INT_EQ
#define REQUIRE_INT_EQ BARO_REQUIRE_INT_EQ
#define CHECK_INT_NE BARO_CHECK_INT_NE
#define REQUIRE_INT_NE BARO_REQUIRE_INT_NE
#define CHECK_INT_LT BARO_CHECK_INT_LT
#define REQUIRE_INT_LT BARO_REQUIRE_INT_LT
#define CHECK_INT_LE BARO_CHECK_INT_LE
#define REQUIRE_INT_LE BARO_REQUIRE_INT_LE
#define CHECK_INT_GT BARO_CHECK_INT_GT
#define REQUIRE_INT_GT BARO_REQUIRE_INT_GT
#define CHECK_INT_GE BARO_CHECK_INT_GE
#define REQUIRE_INT_GE BARO_REQUIRE_INT_GE
#define CHECK_UINT_EQ BARO_CHECK_UINT_EQ
#define REQUIRE_UINT_EQ BARO_REQUIRE_UINT_EQ
#define CHECK_UINT_NE BARO_CHECK_UINT_NE
#define REQUIRE_UINT_NE BARO_REQUIRE_UINT_NE
#define CHECK_UINT_LT BARO_CHECK_UINT_LT
#define REQUIRE_UINT_LT BARO_REQUIRE_UINT_LT
#define CHECK_UINT_LE BARO_CHECK_UINT_LE
#define REQUIRE_UINT_LE BARO_REQUIRE_UINT_LE
#define CHECK_UINT_GT BARO_CHECK_UINT_GT
#define REQUIRE_UINT_GT BARO_REQUIRE_UINT_GT
#define CHECK_UINT_GE BARO_CHECK_UINT_GE
#define REQUIRE_UINT_GE BARO_REQUIRE_UINT_GE
#define CHECK_PTR_EQ BARO_CHECK_PTR_EQ
#define REQUIRE_PTR_EQ BARO_REQUIRE_PTR_EQ
#define CHECK_PTR_NE BARO_CHECK_PTR_NE
#define REQUIRE_PTR_NE BARO_REQUIRE_PTR_NE
#define CHECK_DOUBLE_EQ BARO_CHECK_DOUBLE_EQ
#define REQUIRE_DOUBLE_EQ BARO_REQUIRE_DOUBLE_EQ
#define CHECK_DOUBLE_NE BARO_CHECK_DOUBLE_NE
#define REQUIRE_DOUBLE_NE BARO_REQUIRE_DOUBLE_NE
#define CHECK_DOUBLE_LT BARO_CHECK_DOUBLE_LT
#define REQUIRE_DOUBLE_LT BARO_REQUIRE_DOUBLE_LT
#define CHECK_DOUBLE_LE BARO_CHECK_DOUBLE_LE
#define REQUIRE_DOUBLE_LE BARO_REQUIRE_DOUBLE_LE
#define CHECK_DOUBLE_GT BARO_CHECK_DOUBLE_GT
#define REQUIRE_DOUBLE_GT BARO_REQUIRE_DOUBLE_GT
#define CHECK_DOUBLE_GE BARO_CHECK_DOUBLE_GE
#define REQUIRE_DOUBLE_GE BARO_REQUIRE_DOUBLE_GE
#define CHECK_NEAR BARO_CHECK_NEAR
#define REQUIRE_NEAR BARO_REQUIRE_NEAR
#define CHECK_BYTES_EQ BARO_CHECK_BYTES_EQ
#define REQUIRE_BYTES_EQ BARO_REQUIRE_BYTES_EQ
#define TEST_ABORT BARO_TEST_ABORT
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
#define REQUIRE_STR_EQ BARO_REQUIRE_STR_EQ
#define CHECK_STR_NE BARO_CHECK_STR_NE
#define REQUIRE_STR_NE BARO_REQUIRE_STR_NE
#define CHECK_STR_ICASE_EQ BARO_CHECK_STR_ICASE_EQ
#define REQUIRE_STR_ICASE_EQ BARO_REQUIRE_STR_ICASE_EQ
#define CHECK_STR_ICASE_NE BARO_CHECK_STR_ICASE_NE
#define REQUIRE_STR_ICASE_NE BARO_REQUIRE_STR_ICASE_NE
#define CHECK_ARR_EQ BARO_CHECK_ARR_EQ
#define REQUIRE_ARR_EQ BARO_REQUIRE_ARR_EQ
#define CHECK_ARR_NE BARO_CHECK_ARR_NE
#define REQUIRE_ARR_NE BARO_REQUIRE_ARR_NE
#endif//BARO_NO_SHORT
#endif//BARO_3FDC036FA2C64C72A0DB6BA1033C678B
