# Lua behavior

`atlas::scripting` provides restricted Lua 5.5.1 C++ execution. Standalone sessions
own an interpreter; domain instances share one interpreter per domain. It links
the independent MVVM runtime and diagnostics; it has no UI, renderer, native SDK,
storage or scheduler dependency. Public headers expose only portable C++ types.

See [behavior contracts and examples](../../docs/scripting.md). Source bytes come
from the caller/streamer. Workers receive owned requests and weak completion
transport; they never enter the session's Lua state.

See [Lua development](../../docs/lua_development.md) for pinned authoring tools,
source identities and optional owner-thread LuaPanda attachment. The native
developer host and its loopback adapter are separate from the portable library.

The [shared-domain API](../../docs/scripting_domains.md) composes multiple isolated
behaviors in one physical interpreter, with catalog-only modules, scoped model
listeners, typed project services and value-only domain messages.
