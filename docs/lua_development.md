# Lua development

Atlas embeds **Lua 5.5.1 with C++ linkage**. The embedded interpreter is the execution
environment for Atlas behavior; a system `lua.exe` does not provide its model API,
ownership rules or protected failure behavior. A separate Lua interpreter is not
required to contribute or debug the embedded runtime.

## Windows and VS Code setup

Complete the [C++/Python setup](windows_development.md), activate the Python
environment and run from the repository root:

```powershell
python -m scripts.dev.setup_lua
code --install-extension .tools/lua_dev/luals_extension/extension.vsix --force
code --install-extension .tools/lua_dev/luapanda_extension/extension.vsix --force
code --list-extensions --show-versions
```

The expected extensions are `sumneko.lua@3.19.2` and
`stuartwang.luapanda@3.3.0`. Disable automatic updates for these two extensions in
VS Code's Extensions view. Re-run setup/install after a reviewed tool upgrade.
Do not replace the locally adapted LuaPanda VSIX with its marketplace build.

[The tool manifest](../.ci/lua_tools.yaml) pins source/archive URLs, versions and
SHA-256 hashes. Downloads and installations stay under ignored `.tools/lua_dev`;
checksum mismatches fail before installation. LuaLS **3.19.2** explicitly supports
the `Lua 5.5` runtime setting. `.luarc.json` selects that version, disables runtime
libraries absent from Atlas and loads editor-only [API annotations](../tools/lua_api/atlas.lua).
The workspace selects the pinned server executable rather than whichever server
ships with an installed extension. Completion, type diagnostics, definitions,
references and symbol renaming use LuaLS. The annotations describe the current
model, scoped listener, result handler, catalog import and native service APIs.
Model fields and native service signatures still require runtime schema
validation. A consumer can annotate its own catalog modules and typed wrappers
for project services to give LuaLS the corresponding project-specific types.
The director is owned by the host and routes named entries in a behavior's
returned table. There is no global `director` for scripts to call.

StyLua **2.6.0** formats owned Lua sources using the common Lua 5.4/5.5 syntax
subset. It does not parse every Lua 5.5-only construct. In particular, do not use
the new `global` syntax in owned scripts until the pinned formatter supports it.
This is an authoring policy, not a change to the interpreter or LuaLS version.

```powershell
python -m scripts.ci.format_sources --component scripting --fix
python -m scripts.ci.format_sources --component scripting
python -m scripts.dev.check_lua --component scripting
```

The VS Code tasks expose setup, Lua diagnostics, whole-project/component formatting
and current-file formatting. Bind “Format current source file” to a preferred
shortcut using VS Code's task keybinding command. LuaLS formatting is disabled so
that IDE formatting cannot introduce a different style. CI runs LuaLS and StyLua
only for selected components containing owned Lua sources; it excludes vendor,
downloaded and generated output.

## Embedded debugger

The normal library compiles without debugger support. To enable it in an
application's development build, configure `ATLAS_ENABLE_LUA_DEBUGGER=ON`.
`ATLAS_SHIPPING_BUILD=ON` rejects that combination at configure time and omits the
developer host/tests. Debugger-enabled libraries must not be reused for shipping.
An already compiled library owns its capability; a consumer compile definition
cannot enable an imported binary. `session::debugger_available()` reports it.

The `atlas_lua_debug_host` contributor executable links a separate development
variant in test builds, leaving `atlas::scripting`'s default capability disabled.
It is not installed/exported or packaged with applications. Build it with the
ordinary Windows preset after dependency bootstrap:

```powershell
cmake --preset windows-msvc -DATLAS_COMPONENTS=scripting
cmake --build --preset windows-debug --target atlas_lua_debug_host atlas_scripting_tests atlas_scripting_debug_tests atlas_scripting_benchmark atlas_lua_behavior_tests
```

Ensure the configuration-matched Lua and transitive dependency DLL directories
are on `PATH`, as described in the Windows guide. In VS Code open
`tests/scripting/data/debug_fixture.lua`, set a breakpoint on the call to
`increment`, select “Atlas Lua: UI domain (8818)” and press F5. The adapter starts
a local listener. In a separate initialized terminal run:

```powershell
./build/windows-msvc/tools/lua_debug_host/Debug/atlas_lua_debug_host.exe tests/scripting/data/debug_fixture.lua .tools/lua_dev/luapanda/LuaPanda.lua 8818 ui
```

Inspect `answer` (`41`), use the Variables/Watch/Call Stack views, and step
in/out/over. Continue to finish. To inspect the application domain, select
“Atlas Lua: application domain (8819)” and run the host with `8819 application`
as the last two arguments. That breakpoint shows `answer = 71`.

Each domain run creates both domains on the same coordinator. Two UI behaviors
share one interpreter and the application behavior has its own. All three
execute the same catalog module. The debugger hooks belong to the selected
instance. The terminal reports both untargeted ticks before the selected tick
completes. Omit the final selector for the standalone session fixture.

The native host is a contributor fixture runner. Its bounded file reads are
developer infrastructure. Real application scripts continue to arrive through
the streamer; the scripting library and debugger never open project asset files.
The optional socket implementation lives under `libs/platform/windows/scripting`.
The public transport API exposes portable owned strings and durations, without
Lua or Windows SDK types. Portable validation runs the in-memory agent tests and
host version check without compiling Winsock.

## Sources, hooks and pause behavior

Use `source_asset::source_name` for domain sources and modules, or pass
`load(bytes, "@/absolute/project/scripts/ui/menu.lua")` for a standalone session.
Use a normalized, stable, unique authoring path with forward slashes. The `@`
prefix identifies a file in Lua debug information. Catalog identity selects the
asset and the source name maps its bytecode to the editor. The API consumes bytes
already supplied by the caller and does not resolve/open that path. Configure
LuaPanda's `cwd` to the external project root and use `autoPathMode=false`.
For relocated workspaces use the adapter's `docPathReplace` mapping. Identical
filenames in different directories must retain different full identities.

Attach to a healthy instance after creation and outside an active call:

```cpp
ui.register_source({"menu", revision, "@/absolute/project/scripts/ui/menu.lua", script_bytes});
auto menu = ui.create({"menu", "hud"}); // The composition root has already bound the hud model.
ui.attach_debugger(menu, {verified_agent_bytes, local_transport, 8818});
model->begin_frame(frame);
ui.begin_frame(frame);
ui.call(menu, "tick");
ui.detach_debugger(menu);
```

The supplied agent is trusted development code. Use the setup-produced, verified
source, not user/project behavior as an agent. Its private environment contains
inspection and protected-call facilities required by LuaPanda. Application
handlers retain their restricted environment; they do not receive `debug`,
`package`, sockets or protected calls. The agent's module loader permits only
the private builtins/socket facade. Filesystem loading and OS commands are absent.
LuaPanda watches/REPL are privileged development operations, not a sandbox for
untrusted remote users.

The owned native hook combines the instruction quota and the agent subscription.
Attaching, changing or removing the agent's subscription cannot overwrite the
quota hook. Debug-attached execution checks each instruction, because calling a
Lua line hook resets Lua's instruction counter; the normal interpreter retains
its lower-overhead 100-instruction interval. Debugger agent work itself executes
inside a hook and is excluded from application instruction accounting. The
trusted attach/bootstrap also bypasses application instruction quotas, while
retaining the VM allocation cap. Debug-attached runs are not performance samples.

A breakpoint suspends the VM owner synchronously; workers never enter the paused
stack. No simulation step or lifecycle event advances during that owner call.
The host supplies logical time/delta and must exclude paused wall time when
admitting the next simulation step. Diagnostics label these calls
`script.call.debug`; durations include debugger interaction and waiting. Attach
has a separate `script.debugger.attach` span. Profiling still follows the shared
auto/on/off build policy. There is no separate Lua instruction profiler hook to
replace the owned dispatcher.

Detach closes transport and releases registry references without executing agent
teardown code. Successful reload retires the old attachment; explicitly attach
using the new handle returned by `domain::reload`. Failed reload preserves the
prior generation and attachment. Each attached instance needs its own transport.
Disconnect does not transfer VM ownership or cancel an otherwise valid handler.

## Compatibility and verification

The pinned LuaPanda source needs one reviewed Lua 5.5 adaptation: serialize a
generic-for key into a separate local instead of assigning its immutable control
variable. Setup requires the exact source pattern and preserves upstream license
comments. The pinned VSIX needs a separate adapter change from an all-interface
TCP listener to **127.0.0.1**. Its remaining files/licenses are retained. Both
adaptations fail explicitly if the pinned upstream source changes.

Always use `useCHook=false`: LuaPanda's precompiled C-hook DLLs are not compatible
with Atlas's Lua 5.5.1 C++ ABI. The owned socket facade replaces LuaSocket; do not
load a system LuaSocket DLL into this VM. Outbound native connections and the
adapted editor listener use loopback only. No nonlocal transport, authentication
or remote deployment mode is supplied. Do not expose or port-forward this local
development protocol.

CTest drives the real embedded host over TCP for a standalone session and each
domain selection. The domain tests check that only the selected instance stops
on a catalog module breakpoint, then inspect its stack, locals and watches and
exercise step in/out/over and disconnect. Core tests additionally check restricted
globals, quotas, source names and attachment cleanup after reload/removal. These
checks do not require VS Code or a system interpreter:

```powershell
ctest --preset windows-debug -L '^scripting$' --output-on-failure --no-tests=error
python -m scripts.dev.debug_smoke build/windows-msvc/tools/lua_debug_host/Debug/atlas_lua_debug_host.exe .tools/lua_dev/luapanda/LuaPanda.lua tests/scripting/data/debug_fixture.lua --domain ui
```

If breakpoints do not bind, verify the selected port, exact full source identity,
`luaFileExtension="lua"` (without a dot), `cwd`, and `useCHook=false`. Restore a
fresh verified agent after tool upgrades; do not debug with an arbitrary copy
from the marketplace extension. VM allocation limits also cover debugger data;
very large watches can fail the protected call and require reload.

## Authored Lua tests

The test host runs [LuaUnit 3.5](https://github.com/bluebird75/luaunit/tree/LUAUNIT_V3_5)
on the same Lua 5.5.1 C++ dependency as Atlas. The tool manifest pins its commit,
BSD-2-Clause license and archive checksum. Setup installs the framework and
license under `.tools/lua_dev/luaunit`. Tarantool's `luatest` requires Tarantool
and LuaJIT facilities, so it is incompatible with this runtime.

With dependencies prepared, run the authored suites without VS Code:

```powershell
python -m scripts.dev.setup_lua --tools luaunit
cmake --preset windows-msvc -DATLAS_COMPONENTS=scripting
cmake --build --preset windows-debug --target atlas_lua_behavior_tests
ctest --preset windows-debug -R '^lua_behavior_suites$' --output-on-failure --no-tests=error
```

The `scripting` CI component installs the pinned framework, builds the host and
runs the CTest registration. LuaUnit writes individual case results to
`build/<preset>/reports/lua-behavior-<configuration>.xml`. CI retains that JUnit
report, including case failures when the runner can produce it. Missing tooling,
no executed cases and failed assertions cause a nonzero exit. Shipping builds
omit the host.

Suites under `tests/scripting/lua` return named LuaUnit groups. The runner passes
the framework, fixture API and fixture source reader as arguments. Group methods
start with `test`; use distinct names for parameter cases. Pure functions can be
tested directly. Place behavior source in `data/` and publish its bytes under a
catalog identity to exercise it through the embedded runtime.

`atlas.with_fixture(function() ... end)` creates two independent domains and
typed fixture models with integer `count` and `mirror` fields, an integer
`signal` action and a string `changed` event. Every callback exit, including a
LuaUnit failure, destroys its domains and releases the fixture. The driver uses
the following explicit phases:

| Operation | Test purpose |
| --- | --- |
| `publish(side, id, revision, bytes)` and `create(side, id)` | Publish a catalog source and return a fixture-local instance handle. Use `ui` or `application` for `side`. |
| `begin_frame(number)` | Advance both fixture models and domains with a deterministic frame identity. |
| `call(handle, name, ...)` and `dispatch(side, name, ...)` | Enter one instance or visit a domain in registration order. |
| `synchronize(side)`, `flush(side)` and `read(side, field)` | Apply listener edits, commit model demands and inspect committed state. |
| `take_requests(side)`, `complete(index, success, ...)` and `deliver_results(side)` | Retain the latest request batch, complete by one-based index and deliver the ready prefix. Test completion order without sleeps or worker threads. |
| `reload(handle)` | Return a new fixture-local handle on success and retain the old handle for stale-generation assertions. |
| `post(handle, sequence, frame, name, ...)` and `deliver_messages(side)` | Submit and deliver owned scalar ingress at the specified boundary. |

The runner's LuaUnit assertions and file reads execute on a separate trusted
stack. The behavior runs via `atlas::scripting::domain` with the normal capability
and instruction limits. Test failures preserve catalog source names and Lua
line diagnostics. LLVM coverage measures the C++ host and exercised native code.
There is no Lua line coverage collector in this profile. A future Lua 5.5
collector must subscribe through the owned hook dispatcher so it cannot replace
instruction quotas or debugger hooks. Keep native ownership, allocator and
concurrency tests in GoogleTest, and keep project-specific tests in the consumer
workspace.

## Consumer workspaces

Keep scripts, content XML and project configuration in the consuming project,
outside the Atlas submodule/subtree. Do not edit the dependency's tracked
configuration to point to a particular project. Copy/adapt the small editor
configuration into the consumer workspace: point `workspace.library` at the
pinned Atlas `tools/lua_api` directory, the LuaLS executable at that checkout's
ignored installation, and debugger `cwd` at the consumer root. The composition
root supplies source bytes, identities, models, domains, allocator and development
transport. The project setup tool does not yet generate those configurations.
