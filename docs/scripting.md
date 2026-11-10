# Lua behavior

`atlas::scripting` provides independent session-owned Lua behavior over an
`atlas::mvvm::view_model`. `atlas::ui_scripting` adapts UI events to that behavior.
Neither the model runtime nor Lua core depends on the editor, renderer or UI.
The application owns the shared job system and all phase boundaries.

## Dependency and ownership

The pinned vcpkg manifest selects Lua **5.5.1** with the `cpp` feature. The runtime
links `unofficial::lua::lua-cpp` privately and includes its C++ headers, not
`lua.hpp`'s C-linkage wrapper. Lua errors unwind C++ scopes with exceptions rather
than jumping over live RAII objects. Calls and library initialization execute
inside `lua_pcall` trampolines; native standard exceptions retain their original
type. The Lua stack and instruction hook are restored on every protected exit.
The public API exposes no Lua pointer or vendor header.

The creating thread owns load, calls, observers, frame admission, result acceptance
and teardown. Reentrant or foreign calls fail before VM access. A session retains
its model, diagnostics and shared PMR resource. Models use the same XML-generated
schema/runtime contracts as native factories; no per-model C++ subclass is needed.

`load(source)` consumes caller-provided text bytes. It opens no files and accepts
no bytecode. The streamer remains the application-managed storage boundary.
Scripts return a table of named functions. Top-level initialization can resolve
slots/read committed values, but cannot produce commands or native work.

```lua
local xp = model.field("xp")
local gain = model.action("gain")
return {
    tick = function(dt)
        -- Lightweight event production; expensive computation belongs to native work.
    end,
    pointer_down = function()
        model.invoke(gain, 10)
        return false, true, false, true, false
    end,
    gain = function(amount)
        model.add_integer(xp, amount)
    end
}
```

Fields, actions and events resolve to namespace-tagged integer slots scoped to the
current model schema. Cache them in initialization; hot operations need no string
lookup. Do not persist these indices as asset IDs or transfer them across sessions.
`model.get(field)` reads committed state. `set`, `add_integer`, `add_number`,
`invoke` and `emit` enqueue exact typed demands. Integer and number are distinct;
use a floating Lua literal such as `1.0` for a number field. Native C++ and Lua
both preserve signed 64-bit integers and double values.

## Model dispatch and rollback

The composition root calls `model.begin_frame(id)` then `behavior.begin_frame(id)`.
The identities must match and remain healthy. `behavior.call(name, arguments)`
enters an optional handler and returns at most 32 scalar values. Missing handlers
are no-ops; a present nonfunction is an error. Input values cannot be nil/tables,
nonfinite numbers or invalid XML text. An optional result-type contract validates
the returned prefix before admitting any commands.

Each protected call stages its commands/work privately. Only a successful return
with valid outputs admits the whole model batch and its native requests. Model
`submit(commands)` preserves preexisting queue contents/counters if any demand is
invalid, exceeds model budgets or fails allocation. It does not flush observers
or change committed columns. It checkpoints pending transport using the model's
PMR; this boundary has copying/allocation cost, especially with queued text.
Single-demand native producers can still use the existing direct API.

`watch(field_or_action_or_event, handler)` compiles a model subscription between
flushes. Notifications invoke the handler with their typed arguments on the
owner. Script-produced demands append to the model's iterative bounded dispatch,
without recursive flushes. Independent numeric producers should use ordered
deltas rather than reading a committed value and queuing competing replacements.
`unwatch(token)` is idempotent. Subscriptions weakly reference the interpreter
owner and are detached on successful reload/shutdown.

On an admitted script/native failure, the call's staged demands disappear, the
session latches failure and outstanding native work is cancelled. Lua locals and
earlier successful calls are not rolled back. Recovery requires reload because
arbitrary Lua heap mutations cannot be transactionally undone. A failing model
observer also latches MVVM dispatch: committed values remain and later pending
demands are discarded under the model's existing recovery policy. The caller must
explicitly reset that model before admitting another frame.

## UI phases

Register a retained `ui_scripting::event_handler` through `ui::input_session`.
Admit the behavior/model frame before `input_session.begin_frame`, because that
operation delivers `start`/`enable` immediately. Functions use the existing event
names: `start`, `enable`, `disable`, `end`, `process_input`, `tick`, `late_tick`,
`end_frame`, pointer/key/focus events, `action` and `ensure_visible`.
Use `["end"] = function(...) ... end` because `end` is a Lua keyword.

Every UI handler receives these scalar arguments, in order:

1. Frame delta in seconds (number).
2. Route phase (integer corresponding to `ui::route_phase`).
3. Local X and Y (two numbers).
4. Semantic key ID (integer).
5. Semantic action ID (decimal text, empty when absent).
6. Action phase (integer, -1 when absent).
7. Whether the original target is alive (boolean).

Handlers can return a boolean prefix in order: stop propagation, capture pointer,
release pointer, focus and blur. Missing flags are false. Nonboolean/excess flags
reject the entire call before its model demands are admitted. The input session
enforces its existing event-specific response rules; direct lifecycle responses
are ignored. Scripts never receive an EnTT registry, element pointer or mutable
tree. Bind ViewModel fields through `atlas::ui_mvvm` to publish UI changes.

The owner chooses the schedule: dispatch lightweight events, flush model demands,
publish UI bindings, seal/apply UI commands, then run joined layout/paint/native
work before the next serial phase. No callback implicitly advances this schedule.
The shared game/editor demonstration currently retains its native behavior;
applications opt into these libraries rather than creating a hidden global VM.

## Asynchronous native work

`model.request(name, ...)` stages a native operation with at most 32 scalar inputs
and returns a generation-local request ID. `take_requests()` transfers each owned
description once. The host selects allowed native operations and submits them to
its injected job dispatcher or another appropriate asynchronous service. No job
receives a session, Lua state, observer or live model reference.

```cpp
for (auto& request : behavior.take_requests()) {
    jobs.run("native behavior work", std::move(request),
        [](atlas::jobs::job_context&, auto& work) {
            if (work.cancellation.stop_requested()) { return; }
            const std::array<atlas::mvvm::value, 1> result{std::int64_t{42}};
            (void)work.complete(result);
        });
}
// Later, in an explicit owner phase:
behavior.deliver_results();
model.flush();
```

The caller retains job handles when it needs a joined boundary. Each request
contains a generation cancellation token and weak value-only mailbox access.
`complete(values, success)` accepts once and returns false for duplicate, retired
or cancelled tickets. Invalid/oversized result payloads throw without consuming
the ticket. There are at most 30 result values, reserving two handler arguments.

`deliver_results()` drains the ready prefix in submission order into optional
`on_result(id, success, ...)`. Worker arrival cannot reorder callbacks; an earlier
unfinished request blocks later acceptance. Calls staged during this pump are not
recursively pumped. Availability at an explicit phase boundary remains an input
to the application; replay must record it. This is not a claim of deterministic
operating-system scheduling or portable Lua hash-table iteration.

## Limits, reload and diagnostics

### Authored observations

The composition root can expose the `diagnostic` table by passing
`script_diagnostics_options` with `enabled = true` to a `session` or `domain`.
The existing `diagnostics::services` supplies the optional logger and profiler.
Supply a `diagnostics::metrics` registry in the script options to accept numeric
observations. The host owns all destinations and delivery policy. The API creates
no storage, exporter, sampler or network connection.

```cpp
atlas::scripting::domain ui{
    limits, memory, {logger, profiler}, {.enabled = true, .metrics = ui_metrics}};
```

An opted-in handler can emit a structured log, wrap a callback in a trace region,
and update a counter or gauge:

```lua
return {
    enable = function()
        diagnostic.log("info", "menu", "Menu opened")
        diagnostic.increment("menu_opens", 1, { screen = "inventory" })
        diagnostic.gauge("item_count", 12, { screen = "inventory" })
    end,
    tick = function(delta)
        return diagnostic.scope("calculate", function(value)
            return value * 2
        end, delta)
    end,
}
```

Only admitted handlers can record observations, including model callbacks and
`on_result`. The logger and metric registry receive names prefixed with `lua.`.
Logs include the behavior entry source identity, domain, instance, generation
and logical frame. Authored trace events use the `lua.scope` category and copy
that provenance into `args.origin`. Result handlers include the native request
ticket. Standalone sessions use domain zero and a process-local instance ID.
Metrics retain the caller's explicit dimensions. Independent registries isolate
domains, and sharing a registry deliberately aggregates identical series.

Names, categories and dimension keys allow up to 64 ASCII letters, digits,
underscores, periods and hyphens. Logs allow up to 2,048 message bytes and
metrics accept up to eight dimensions with values of up to 128 bytes. Messages
and dimension values must be valid XML-compatible UTF-8. Counters accept finite
nonnegative increments, and gauges accept finite values. The native registry
fixes the kind on the first accepted observation and enforces its configured
series and retention budgets. Authored calls with a destination share a limit of
64 observations per handler. Invalid inputs and a spent handler budget fail
through the protected boundary. A log or metric function returns `false` when
its destination is absent or declines admission. Dimension contents are inspected
only when a registry is injected. Accepted observations are immediate and remain
observable if the handler's model and native work demands later roll back.

`scope` forwards callback arguments, results and errors. Each captured region
closes on return or protected unwinding. Reload and shutdown leave no open
regions. `ATLAS_PROFILING=auto` enables probes in Debug, `on` enables them in every
configuration, and `off` compiles them out. Without an enabled capture or during
debugger attachment, the callback still runs without an authored timestamp or
label allocation. Debugger wall time remains visible in the existing
`script.call.debug` span. Scopes use no extra Lua hook. The host may opt in to
logs and metrics in shipping builds, while profiler behavior follows the build
policy and the debugger remains unavailable. Worker jobs never call the Lua API.

Run `atlas_scripting_benchmark --diagnostics` in Release with
`ATLAS_PROFILING=on` to compare the same warmed handler with capture enabled and
disabled. Timings are observations and include the existing native call span.

### Runtime limits and recovery

Defaults bound live Lua allocations to 4 MiB, source to 256 KiB, staged demands to
64 per call, aggregate command/request text to 64 KiB per call, calls to 256 per
frame and Lua instructions to 100,000 per protected operation. Native work allows
64 pending entries and 4 MiB retained transport payload. Inputs and returned values
have separate 64 KiB text totals. Policy caps are documented on `session_limits`.
PMR reallocation temporarily owns old/new blocks together; its transient upstream
peak can exceed the live Lua-byte quota. Standalone reload briefly retains two
interpreters; domain reload retains candidate/current environments inside the
same interpreter and aggregate Lua quota.
Host command/checkpoint vectors, schemas, result copies and caller-retained job
payloads have separate allocation costs. These are not whole-process limits.

Only base/table/string/math/UTF-8 libraries are opened. File/process/package/debug/
coroutine APIs, dynamic loading, print/warn, script `pcall`/`xpcall`, user metatable
installation, unordered iterator entry points and math randomness are omitted.
This prevents scripts from catching instruction-quota traps or installing Lua
finalizers. C library operations are bounded by memory/source/protocol policies,
but instruction hooks cannot preempt time inside a C library call. This is a
trusted behavior environment with misuse bounds, not a hostile-code sandbox or a
hard real-time execution guarantee.

Reload builds a candidate VM first. Failed load preserves the old generation,
observers and native work. Successful load cancels old tickets, detaches watches,
resets Lua state/frame counters and installs a fresh mailbox. Register new watches
and admit a matching model frame afterward; no implicit state migration occurs.
`shutdown()` cancels work and destroys its logical context without running script callbacks or
waiting for host jobs. The application still owns stopping/joining those jobs and
must deliver desired UI shutdown lifecycle events before closing behavior.

A session binds one root ViewModel and one returned handler table. The
[domain API](scripting_domains.md) composes per-element/menu instances and named
model contexts in one shared interpreter, with a Lua director and scoped
subscriptions. The UI adapter can target a domain instance. XML persistence of
these authoring relationships is separate from the execution API.

Diagnostics log successful reload/shutdown at debug severity and protected
failures at warning severity. Profiling covers `script.load`, `script.call`,
`script.results` and `ui.script.event` with the shared
`ATLAS_PROFILING=auto|on|off` policy. No global sink/file/telemetry is created.

The [Lua reference manual](https://lua.org/manual/5.5/manual.html) describes the
protected API, instruction hooks and C++ error mechanism. The pinned
[vcpkg Lua port](https://github.com/microsoft/vcpkg/tree/d4c0a46cb8f63e5c9fa007d13baf1cb3e43bf7e3/ports/lua)
defines the C++ binding target used by this library.
