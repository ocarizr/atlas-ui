# Lua behavior

`atlas::scripting` owns a restricted Lua 5.5.1 C++ interpreter per session. It links
the independent MVVM runtime and diagnostics; it has no UI, renderer, native SDK,
storage or scheduler dependency. Public headers expose only portable C++ types.

See [behavior contracts and examples](../../docs/scripting.md). Source bytes come
from the caller/streamer. Workers receive owned requests and weak completion
transport; they never enter the session's Lua state.
