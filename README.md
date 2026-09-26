# Baro

A small C99 unit-testing framework for hosted Windows and POSIX systems.
Write tests beside implementation code, with assertions, independent subtest
branches, and a command-line runner. C++ is not supported.

```c
#include "baro.h"

static int add(int a, int b) { return a + b; }

TEST("[math] addition") {
    CHECK_INT_EQ(add(2, 3), 5);
    REQUIRE(add(-1, 1) == 0);
}
```

This exact example is compiled and run by the test suite.

## Integration

Vendor `baro.h`, `baro.c`, `baro_process.h`, and `baro_main.c`, then compile your test sources:

```sh
cc -std=gnu99 -DBARO_ENABLE -Iext/baro app.c ext/baro/baro.c ext/baro/baro_main.c -lm -o tests
./tests
```

Use the POSIX feature environment on Unix (GNU99 supplies it), or MSVC's C
mode on Windows. Registration uses compiler constructor support: GCC, Clang,
AppleClang, and MSVC are the supported compiler families. Freestanding targets
are outside the current scope.

With CMake 3.20 or newer:

```cmake
add_subdirectory(ext/baro)
add_executable(tests app.c)
target_link_libraries(tests PRIVATE baro::main)
enable_testing()
add_test(NAME app_tests COMMAND tests)
```

Installed packages support `find_package(baro CONFIG REQUIRED)`. `baro::main`
provides the default entry point; `baro::baro` provides the runtime only.
Both propagate `BARO_ENABLE`. Production targets should not link these targets
or define `BARO_ENABLE`; test bodies then remain unregistered and can be removed
by optimization. `BARO_BUILD_TESTS` defaults off when consumed as a subdirectory.
`BARO_SANITIZERS` is opt-in and instruments the runtime and linked consumers.

Custom runners call `baro_run(argc, argv)` once. Dispatch
`baro_is_child(argc, argv)` to `baro_run` **before parent-only setup** when using
isolation. Child processes repeat normal C runtime initialization.

Tests inside static archives need an explicitly referenced anchor function in
each test object, or the platform's whole-archive link option. Alternatively,
link test object libraries directly. See `tests/consumer` for an anchor example.

## Assertions

`CHECK` records a failure and continues. `REQUIRE` records a failure, runs
registered cleanup, and ends the current top-level test. All assertions must
execute on the runner thread; worker threads should return results to it.

- `CHECK(expr)` / `CHECK_FALSE(expr)` test truth values.
- `CHECK_EQ`, `NE`, `LT`, `LE`, `GT`, `GE` preserve C operand types.
- `CHECK_INT_EQ`, `UINT_EQ`, `PTR_EQ`, `DOUBLE_EQ` convert operands to
  `intmax_t`, `uintmax_t`, object pointers, or `double`, and display values.
- `CHECK_NEAR(a, b, absolute, relative)` accepts either tolerance. Tolerances
  must be finite and nonnegative. NaNs always fail; identical infinities pass;
  other comparisons involving infinity fail. Signed zeros compare equal.
- `CHECK_STR_EQ`, `STR_NE`, `STR_ICASE_EQ`, `STR_ICASE_NE` compare strings.
  Two null pointers compare equal; null differs from every non-null string.
- `CHECK_BYTES_EQ(a, b, count)` compares bytes. `CHECK_ARR_EQ(a, b, count)`
  and `CHECK_ARR_NE` compare array object representations, including padding;
  they are not element-value comparisons for structs or floating-point values.

Each has a `REQUIRE` counterpart and a `BARO_`-prefixed form. Define
`BARO_NO_SHORT` to omit short names. Generic, string, and array assertions
accept an optional trailing string-literal description. Typed assertions use
fixed argument lists. Scalar assertion operands are evaluated once.

Standard `assert` is unchanged unless you define `BARO_REPLACE_ASSERT` before
including `baro.h`; that option replaces it with `BARO_REQUIRE`. Include Baro
after `<assert.h>` when opting in.

## Cleanup and subtests

`baro_defer(callback, &payload, sizeof(payload))` copies the payload into owned
storage. The callback receives a pointer to that copy. Registrations run in
reverse order after each subtest traversal or a hard failure. Pointers inside
payloads must refer to heap/static storage, never expired stack objects. Copy
pointers to allocated resources and release those resources in the callback.
Callbacks should not register further cleanup; a failed requirement in a
callback ends that callback and remaining callbacks still run.

`SUBTEST("description") { ... }` creates a branch. The top-level body is rerun
for each leaf, allowing fresh setup for each traversal. A failed `REQUIRE` ends
all remaining traversals of that top-level test. Avoid `return`, `break`, or
`goto` escaping a subtest; use assertions to control failure.

Cleanup is not guaranteed after an abort, crash, or forced termination.
In-process `--recover-abort` (`-r`) is explicitly best-effort: jumping out of a
library assertion does not repair locks or global state. Prefer isolation for
code that may abort. Ordinary Baro requirements do not raise a signal.

## Runner

- `-a`: report passing tests.
- `-o`: show all stdout, including passing tests.
- `-e`: suppress stderr.
- `-s`: stop after the first failed test.
- `-t foo,bar`: select descriptions containing `[foo]` or `[bar]`.
- `--test "exact description"`: select by exact description (combined with tags).
- `--list-tests`: list selected tests without running them.
- `--allow-empty`: explicitly allow a selection matching no tests.
- `--junit path.xml`: write JUnit results as well as console output.
- `-h`: help.

Exit status is zero on success and nonzero on test, argument, or report-writing
failure. No matching tests is an error by default. Test descriptions should be
unique for unambiguous exact selection; duplicates select every matching test.

By default stdout is captured in a temporary file. Each failure shows the final
4096 bytes since the preceding failure in that test, including explicit flushes.
Passing output is discarded. Disk usage grows until capture is reset at a
failure or test boundary; `-o` disables in-process capture.

`-n index -p count` selects one contiguous partition (both values are 1-based).
Partitions differ by at most one test; earlier partitions get any extra tests.
This supports external CI sharding and does not create processes by itself.

## Development

```sh
cmake -S . -B build -DBARO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite tests passing/failing behavior, diagnostics, cleanup, capture boundaries,
CLI validation, JUnit escaping, and independent source/installed consumers.
CI covers Debug/Release with GCC, Clang, AppleClang, and MSVC. Local validation
in this development session has only run on macOS; CI results determine support
on other platforms.

MIT license; see [LICENSE](LICENSE).

## Isolated execution

```sh
./tests --isolate
./tests --isolate --jobs 4 --timeout 10s
./tests --isolate -n 2 -p 5 --junit shard2.xml
```

Baro launches a fresh instance of the same executable for each top-level test.
Its subtest traversals run together in that child. The parent owns scheduling,
results, stdout/stderr capture, and reports; no external supervisor is required.
Static/global memory is fresh for every test. Files, network ports, and other
external resources remain shared, so parallelism is opt-in.

`--jobs` defaults to one. `--timeout` accepts positive seconds, or an `s`/`ms`
suffix, and includes child startup. Both require `--isolate`. There is no timeout
by default. A timeout terminates the test's process group on POSIX or job on
Windows, including descendants. Completed test children also have their remaining
descendants terminated. Escaping those process groups/jobs is unsupported.

With `-s`, no new tests launch after the parent observes a failure. Already
running tests finish or reach their deadlines. Console output is grouped by
completed child; JUnit entries remain in selection order. Aborted/crashed tests
have no completed assertion totals; they still count as failed tests.

`TEST_ABORT("description") { ... }` (or `BARO_TEST_ABORT`) expects SIGABRT and
requires isolation. Normal return, premature exit, and timeout are failures.
A preceding failed Baro assertion also fails an expected-abort test.
Ordinary `TEST` treats every abort as failure. Isolation observes an abort via a
small child signal handler that reports it and exits, without attempting to
resume damaged code. If code under test replaces that handler, an unexpected
termination is still a failure but expected-abort recognition may be unavailable.

Isolation uses private temporary result files and captured output files. `-o`
shows all captured stdout; otherwise failed children show a final 4096-byte tail.
Stderr is shown unless `-e` is set. Output is presented after child completion,
not streamed live. Temporary disk space must accommodate output until completion.
