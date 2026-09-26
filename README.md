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
baro_discover_tests(tests)
```

Installed packages support `find_package(baro CONFIG REQUIRED)`. `baro::main`
provides the default entry point; `baro::baro` provides the runtime only.
Both propagate `BARO_ENABLE`. Production targets should not link these targets
or define `BARO_ENABLE`; test bodies then remain unregistered and can be removed
by optimization. `BARO_BUILD_TESTS` defaults off when consumed as a subdirectory.
`BARO_SANITIZERS` is opt-in and instruments the runtime and linked consumers.

Custom runners call `baro_run(argc, argv)` once. Dispatch
`baro_is_child(argc, argv)` and `baro_is_discovery(argc, argv)` to `baro_run`
**before parent-only setup** when using isolation or CTest discovery. Child processes repeat normal C runtime initialization.

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
requires isolation or the CTest adapter described below. Normal return, premature exit, and timeout are failures.
A preceding failed Baro assertion also fails an expected-abort test.
Ordinary `TEST` treats every abort as failure. Isolation observes an abort via a
small child signal handler that reports it and exits, without attempting to
resume damaged code. If code under test replaces that handler, an unexpected
termination is still a failure but expected-abort recognition may be unavailable.

Isolation uses private temporary result files and captured output files. `-o`
shows all captured stdout; otherwise failed children show a final 4096-byte tail.
Stderr is shown unless `-e` is set. Output is presented after child completion,
not streamed live. Temporary disk space must accommodate output until completion.

## CTest and IDE integration

`baro_discover_tests(tests)` is available after `add_subdirectory` or
`find_package(baro CONFIG REQUIRED)`. Call it once per executable, after
`enable_testing()`. It registers each top-level test separately; subtest branches
stay together. Use it instead of a suite-wide `add_test` to avoid running twice.

```cmake
baro_discover_tests(tests
    TEST_PREFIX "app::"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    ENVIRONMENT "APP_MODE=test"
    LABELS unit
    TIMEOUT 10
    DISCOVERY_TIMEOUT 10
    PROPERTIES RESOURCE_LOCK database)
```

Names are `<prefix><description><suffix>` for unique descriptions. Duplicate
descriptions gain a source-relative filename, and duplicates within that file
also gain an occurrence number. Unrelated test insertions, blank lines, and
checkout relocation preserve names. Adding/reordering identical descriptions
can change their disambiguation. Numeric IDs are used only to execute tests. The default prefix is the target
name followed by `::`; `TEST_PREFIX ""` removes it. `TEST_SUFFIX` defaults empty.
Tags become CTest labels alongside `LABELS`. Duplicate descriptions and names
containing quotes, semicolons, or newlines remain distinct and selectable.
`TIMEOUT` is seconds per test (unset by default); `DISCOVERY_TIMEOUT` defaults to
10 seconds. The working directory defaults to the current binary directory.
`ENVIRONMENT` entries also apply during discovery. `PROPERTIES` accepts CTest
name/value pairs and overrides generated defaults. Avoid properties that redefine success, such as
`WILL_FAIL` or `PASS_REGULAR_EXPRESSION`, unless that is intentional. `EXTRA_ARGS` accepts only
output flags `-a`, `-o`, and `-e`, including clusters.

```sh
cmake --build build
ctest --test-dir build -N
ctest --test-dir build -L unit -j 4 --output-on-failure
ctest --test-dir build -R addition --output-on-failure
ctest --test-dir build --rerun-failed --output-on-failure
```

For multi-configuration generators, build with `--config Debug` and pass
`-C Debug` to CTest. Discovery runs at each CTest startup, including IDE listing,
and refreshes after rebuilding without reconfiguring. An unbuilt executable
produces a failing `NOT_BUILT` entry. An empty executable registers no tests;
use CTest's `--no-tests=error` if an empty project should fail. Cross-built
executables require the target's `CROSSCOMPILING_EMULATOR` and `RUNNER` pointing
to a host-built `baro_ctest` adapter.

CTest owns parallel scheduling, output capture, and deadlines. A small native
adapter launches one test executable and requires both a successful exit status
and a private completion record. It does not start Baro's isolation scheduler. A POSIX lifetime guard or Windows
job ensures that terminating the adapter also terminates test descendants.
Ordinary tests record success only after tests and cleanup complete. `TEST_ABORT`
records success from its SIGABRT handler only when no Baro assertion has failed.
Normal return from an expected-abort test, premature `exit(0)`/`_exit(0)`, crashes,
and timeouts fail. Custom-main failures after `baro_run` also fail.

The adapter gives each invocation a unique result file in the system temporary directory. Completed invocations remove their records; interrupted
runs can leave `baro-ctest-*` files, which are safe to delete and never reused.
As with isolation, replacing the abort handler is unsupported. Abort handlers
do not resume execution or run cleanup callbacks. Direct debugger invocation
bypasses the adapter and its completion verification.

Discovery runs the executable's initialization and custom main, but no test
bodies. Keep discovery stdout free of application messages. A custom runner can
skip parent setup like this:

```c
if (baro_is_child(argc, argv) || baro_is_discovery(argc, argv))
    return baro_run(argc, argv);
/* Application setup, then baro_run(argc, argv). */
```

`--list-tests-json` emits version 1 JSON with a `tests` array containing `id`,
`name`, `file`, `line`, `expect_abort`, and `tags`. IDs identify exactly one test
in that executable and may change after rebuilding. The CMake helper saves the
inventory under `.baro/<target>/<configuration>.json` and exposes source metadata
as `BARO_SOURCE_FILE` and `BARO_SOURCE_LINE` CTest properties.

For debugging, discover the ID and launch the executable directly:

```sh
./tests --list-tests-json
lldb -- ./tests --test-id 3 -o --diagnostics compiler
# Or: gdb --args ./tests --test-id 3 -o --diagnostics compiler
```

Use the same working directory and environment as CTest. Add `--ctest` when
debugging an expected-abort test; configure the debugger to stop on SIGABRT.
`--diagnostics compiler` prints `file:line: error:` locations for editor links.
Source paths are those supplied by the compiler's `__FILE__`.

IDEs with CTest support can list and run individual cases through this helper.
Source gutters and per-case Debug buttons depend on the IDE's adapter; the
metadata alone does not provide a native Baro plugin. Direct debugger launching
is the fallback. Automated coverage validates CTest behavior, not IDE UI controls.

## Coverage

Use a separate build tree with GCC/gcov or Clang/llvm-cov and gcovr 8.6:

```sh
python -m pip install 'gcovr==8.6'
cmake -S . -B out/coverage -DCMAKE_BUILD_TYPE=Debug -DBARO_COVERAGE=ON
cmake --build out/coverage --target coverage --parallel 4
```

The `coverage` target builds the tests, deletes old profiles, runs tests serially,
and writes HTML, JSON, XML, and text reports under `out/coverage/coverage/`.
It covers `baro.c`, `baro.h`, `baro_process.h`, `baro_main.c`, and `baro_ctest.c`.
Nested consumer builds and the redundant multi-configuration integration run are
not measured. Do not run other tests concurrently in this dedicated build tree.
Instrumentation is incompatible with `BARO_SANITIZERS`. Set
`BARO_GCOV_EXECUTABLE` if the automatically selected gcov tool does not match your
compiler (for Clang this is an `llvm-cov gcov` command).

The initial AppleClang baseline was 91.4% lines and 76.0% branches before adding
direct adapter failure tests. Default regression floors are 85% lines and 65%
branches, adjustable via `BARO_COVERAGE_MIN_LINE` and `BARO_COVERAGE_MIN_BRANCH`.
The GCC/Linux CI job publishes its own report; compiler/platform results are not
interchangeable. Windows-only code is not measured by this workflow.

`profiles.json` records missing object profiles and measurement limitations.
Normal subprocess exits contribute data; `_exit`, abort handlers, and forced
termination may lose their profiles. An object with data can still lack some
subprocess contributions. These paths have behavioral tests even when coverage
cannot observe them. Missing profiles remain visible, empty reports fail, and
coverage thresholds do not replace behavioral checks. No signal-unsafe profiling
flush is added to abort handlers just to increase the percentage.

## Development presets

With CMake 3.20+, Ninja, and a C compiler on `PATH`, run from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Substitute `release` or `sanitizers` to use separate build trees under `out/`.
On Windows, use a Visual Studio developer shell so Ninja can find MSVC. The
sanitizer preset uses AddressSanitizer on MSVC and AddressSanitizer plus UBSan on
GCC/Clang. The coverage preset requires GCC or Clang and gcovr 8.6:

```sh
cmake --preset coverage
cmake --build --preset coverage
```

The coverage build preset also runs tests and generates reports. `ctest --preset
coverage` only reruns tests; it does not reset profiles or regenerate reports.
Select a compiler on first configure, for example `cmake --preset coverage
-DCMAKE_C_COMPILER=gcc` on Linux. Keep machine-specific settings in the ignored
`CMakeUserPresets.json`. Preset schema version 2 preserves CMake 3.20 compatibility;
no newer workflow-preset support is required.
