# `baro`

![CMake Build Status Badge](https://github.com/branw/baro/workflows/CMake/badge.svg)

A C99 unit testing framework that is:

- **modern** -- write your tests alongside your implementation
- **ultra-light** -- just include a 1k LOC header file
- **portable** -- designed for Windows and *nix

## Usage

1. Include `baro.h` in your source files and start writing tests:
    ```c
    #include <baro.h>

    // Write your methods like usual:

    unsigned long utf8_decode(char **s) { /* ... */ }

    size_t utf8_encode(unsigned long code_point, char **buf, size_t len) { /* ... */ }

    // Then, write your tests in the same file (or wherever, really):

    TEST("[encoding] UTF-8 <-> UTF-32") {
        // Perform some setup that will be executed for each subtest
        struct test_case {
            char const *utf8;
            uint32_t utf32[];
        } const
            ascii = {"abCD12!@", {'a', 'b', 'C', 'D', '1', '2', '!', '@', 0}},
            greek = {"κόσμε", {954, 972, 963, 956, 949, 0}},
            emoji = {"\xF0\x9F\x98\x8E\xF0\x9F\x98\xB8", {128526, 128568, 0}},
            *test_cases[] = {
                &ascii, &greek, &emoji,
            };
   
        size_t const num_test_cases = sizeof(test_cases)/sizeof(struct test_case);

        // Require that the two values are equal, failing with a message if not.
        REQUIRE_EQ(num_test_cases, 3, "unexpected number of test cases");

        // Branch out into a subtest
        SUBTEST("encode UTF-32 to UTF-8") {
            for (size_t i = 0; i < num_test_cases; i++) {
                char *str = (char *)test_cases[i]->utf8;

                char buf[512] = {0};
                char *p = buf;
                size_t len = 512;
                for (size_t j = 0; test_cases[i]->utf32[j]; j++) {
                    len -= utf8_encode(test_cases[i]->utf32[j], &p, len);
                }

                // Check that the two strings are equal. Unlike REQUIRE, CHECK
                // statements will continue the test case after encountering a
                // failure.
                CHECK_STR_EQ(buf, str);
            }
        }

        // Branch out into another subtest that can be executed independently
        SUBTEST("decode UTF-8 to UTF-32") {
            for (size_t i = 0; i < sizeof(test_cases)/sizeof(struct test_case); i++) {
                char *str = (char *)test_cases[i]->utf8;
        
                for (size_t j = 0; test_cases[i]->utf32[j]; j++) {
                    CHECK_EQ(utf8_decode(&str), test_cases[i]->utf32[j]);
                }
                CHECK_EQ(utf8_decode(&str), 0);
            }
        }
    }
    ```

   - See the `examples/` directory for common usage patterns.

2. Create a new build target with all the same source files, except the file
   containing the `main` function. Instead, use the provided `baro.c`.
   - With CMake:
     ```cmake
     # add_executable(app app.c main.c)
     
     add_executable(tests app.c ext/baro/baro.c)
     
     enable_testing()
     add_test(app tests)
     ```

3. Define `BARO_ENABLE` for this new build target only (and _not_ for any
   non-test targets). This allows the test code to be optimized away in
   non-test builds.
   - With `gcc`/`clang`, pass `-DBARO_ENABLE`:
     ```plain
     gcc source.c baro.c -o tests -DBARO_ENABLE
     ```

   - With `cmake`, add it as a compile definition:
     ```cmake
     target_compile_definitions(tests PRIVATE BARO_ENABLE)
     ```

4. Build and run the new build target:
    ```plain
    > ./tests
    Running 10 out of 10 tests
    ============================================================
    Check less than failed:
      magic_frombobulate(magic, panacea) < 50
    At magic.c:25
    In: [magic] frombobulation (magic.c:11)
      Under: with elixir (magic.c:16)
        Under: weaker on the second application (magic.c:23)
    ============================================================
    tests:      10 total |     9 passed |     1 failed
    asserts:    18 total |    17 passed |     1 failed
    ```

  - The tests can also be executed through CMake's CTest by running `ctest`
    after building with `cmake`

### Macros

`baro` is a small set of macros. By default, both a long (`BARO_`-prefixed) and
short (un-prefixed) form are defined. The short form can be disabled by defining
`BARO_NO_SHORT`.

#### Assertions

Assertions begin with either `CHECK` or `REQUIRE`. `CHECK` will fail quietly,
allowing the proceeding statements to run, while `REQUIRE` will fail hard and
cause the current test to end immediately.

|`baro.h` code|`assert.h` equivalent|
|----|----------|
|`REQUIRE(x)`|`assert(x)`|
|`REQUIRE_FALSE(x)`|`assert(!x)`|
|`REQUIRE_EQ(a, b)`|`assert(a == b)`|
|`REQUIRE_NE(a, b)`|`assert(a != b)`|
|`REQUIRE_LT(a, b)`|`assert(a < b)`|
|`REQUIRE_LE(a, b)`|`assert(a <= b)`|
|`REQUIRE_GT(a, b)`|`assert(a > b)`|
|`REQUIRE_GE(a, b)`|`assert(a >= b)`|
|`REQUIRE_STR_EQ(a, b)`|`assert(!strcmp(a, b))`|
|`REQUIRE_STR_NE(a, b)`|`assert(strcmp(a, b) != 0)`|
|`REQUIRE_STR_ICASE_EQ(a, b)`|`assert(!strcmpi(a, b))`|
|`REQUIRE_STR_ICASE_NE(a, b)`|`assert(strcmpi(a, b) != 0)`|

`REQUIRE(a < b)` and `REQUIRE_LT(a, b)` evaluate the same C comparison.
Numeric assertions preserve the operands' types and evaluate each operand
once. Comparison failures report the expression and source location; string
and array assertions also display the compared values.

String assertions treat two null pointers as equal, and a null pointer and any
string (including an empty string) as unequal. Failed comparisons display null
operands as `[null]`.

All these macros also accept a description as an additional parameter:

```c
REQUIRE(file_exists("foo.txt"), "need test data file");
CHECK_STR_EQ(get_name(), "testuser", "name should be populated");
```

Note that an `assert(x)` macro is also provided. It is functionally equivalent
to `BARO_REQUIRE(x)`. Assertions in libraries and other files without the
`baro.h` header will still be detected as test failures via a `SIGABRT` handler.

#### Subtests

Subtests allow for test cases to be arranged in a tree: the outermost `TEST`
scope is the root, and each `SUBTEST` creates a child node. A test with
multiple subtests will be executed once for every leaf node, meaning that
`SUBTEST` branches at the same level can work independently. For example:

<table><thead><tr>
<th>Code</th>
<th>Output</th>
</tr></thead><tbody><tr>
<td><pre>TEST("i like subtests") {
    printf("begin\n");
    SUBTEST("1") {
        printf("1\n");
        SUBTEST("1.1") {
            printf("1.1\n");
        }
    }
    SUBTEST("2") {
        printf("2\n");
        SUBTEST("2.1") {
            printf("2.1\n");
        }
        SUBTEST("2.2") {
            printf("2.2\n");
            SUBTEST("2.2.1") {
                printf("2.2.1\n");
                SUBTEST("2.2.1.1") {
                    printf("2.2.1.1\n");
                }
                SUBTEST("2.2.1.2") {
                    printf("2.2.1.2\n");
                }
            }
        }
        SUBTEST("2.3") {
            printf("2.3\n");
        }
        SUBTEST("2.4") {
            printf("2.4\n");
        }
    }
    printf("\n");
}</pre></td>
<td><pre>

begin
1
1.1

begin
2
2.1

begin
2
2.2
2.2.1
2.2.1.1

begin
2
2.2
2.2.1
2.2.1.2

begin
2
2.3

begin
2
2.4
</pre></td>
</tr></tbody></table>

Hard failures (with `REQUIRE`) in a subtest will bubble-up and fail the entire
test case.

### Command-line arguments

The test runner accepts a few arguments:

- `-a` shows **a**ll test results, even passing ones
- `-o` shows all standard **o**utput (`stdout`), even for passing tests
  - By default, only the last 4096 characters of standard output will be
    shown only for failing tests
  - Captured output uses a temporary file and includes explicitly flushed output.
    Each failure reports the last 4096 bytes since the previous failure in that
    test; output from passing tests is discarded. Temporary disk usage grows
    with output until the next failure or the end of the test.
- `-e` suppresses all standard **e**rror (`stderr`) output
  - `stderr` output will not be shown, even for failing tests, in this case
- `-s` will cause the test suite to **s**top after the first failure
- `-t tag1,tag2` will only execute tests with descriptions containing either
  `[tag1]` or `[tag2]`

#### Partitioning

By default, all test cases are executed in a single thread. You can speed up
execution by creating multiple processes with the partitioning arguments:

- `-n <current_partition>` sets the current partition index (1-based)
- `-p <partition_count>` sets the total number of partitions to make (1-based)

Partitions are contiguous and differ in size by at most one test. When the
test count does not divide evenly, the earlier partitions receive one extra
test each.

For example, if we have 100 tests and want to partition across five processes:
```bash
./tests -n 1 -p 5 # runs tests  1-20 (inclusive)
./tests -n 2 -p 5 #     "      21-40
./tests -n 3 -p 5 #     "      41-60
./tests -n 4 -p 5 #     "      61-80
./tests -n 5 -p 5 #     "      81-100
```

Or, with GNU Parallel:

```bash
parallel ./tests -n {} -p 5 ::: {1..5}
```

## License

`baro` is released under the MIT License. See `LICENSE` for more info.
