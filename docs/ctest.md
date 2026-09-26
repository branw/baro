# CTest and IDE integration

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

