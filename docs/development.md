# Development tooling

## Coverage

Use a separate build tree with GCC/gcov or Clang/llvm-cov and gcovr 8.6:

```sh
python -m pip install 'gcovr==8.6'
cmake -S . -B out/coverage -DCMAKE_BUILD_TYPE=Debug -DBARO_COVERAGE=ON
cmake --build out/coverage --target coverage --parallel 4
```

The `coverage` target builds the tests, deletes old profiles, runs tests serially,
and writes HTML, JSON, XML, and text reports under `out/coverage/coverage/`.
It covers `baro.c`, `baro_process.h`, `baro_main.c`, and `baro_ctest.c`; `baro.h`
holds only declarations and macros.
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
GCC/Clang. Programs built with MSVC's AddressSanitizer load its runtime DLL at
startup; a developer shell has it on `PATH`, and elsewhere the directory that
holds `cl.exe` must be added, or the programs cannot start. The coverage preset requires GCC or Clang and gcovr 8.6:

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
