# Testing and validation

## Discipline

New or corrected production behavior starts with a test that fails for the intended
reason. C++ uses GoogleTest with CMake/CTest. Fixtures, RAII, parameterized and typed
cases share setup without hiding intent. Large serialized scenarios belong in
context-owned XML data files. Generated data has explicit seeds.

Python tests use pytest, function-scoped fixtures, isolated `tmp_path` resources,
plain assertions, specific `pytest.raises` diagnostics and readable parameter IDs.
Cleanup runs after failed assertions and early returns. Concurrent resources stop
and join before referenced memory is released; mutable state is not shared between
tests. Use controlled gates and diagnostic timeouts instead of arbitrary sleeps.

Cover successful behavior, expected errors, boundaries, invariants and cleanup.
Assert forbidden side effects did not occur. Group suites by behavior/context and
keep reusable builders small. Roughly 300 lines is a review signal, not a reason
to fragment cohesive code mechanically.

## Available checks

Component validation includes Debug/Release tests, formatting/documentation,
clang-tidy, MSVC analysis, ASan and line/branch coverage as configured in
`.ci/components.yaml`. SDK-free checks prove portable contracts on the supported
Windows host. Native WARP tests inspect real pixels and exercise lifecycle/pressure;
hardware benchmarks are separate measurements.

No approved universal coverage percentage or production frame-time budget exists.
Coverage does not prove assertion quality or race freedom. Windows native LLVM
ThreadSanitizer is unavailable; stress tests, declared-access checking, ownership
rules and static analysis are not equivalent dynamic race detection.

The generic jobs benchmark, recording benchmark and renderer readiness probe
separate timing from instrumented allocation/trace runs. Report configuration,
hardware, inputs, limits and exclusions. Small synthetic results do not establish
production UI, media or cold-storage throughput.

Follow [Windows checks](windows_development.md),
[source style](source_style.md),
[CI contracts](ci_and_delivery.md) and [Contributing](../CONTRIBUTING.md).
Delivery acceptance and unresolved measurements live in
[issues](https://github.com/ocarizr/atlas-ui/issues), rather than this guide.
