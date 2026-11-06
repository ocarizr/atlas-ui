# UI behavior adapter

`atlas::ui_scripting` forwards the existing UI lifecycle and routed input vocabulary
to an independent behavior session on the owner thread. Scripts enqueue typed
ViewModel demands; the existing MVVM adapter publishes UI properties at explicit
sync points. No live tree, registry, renderer or native device pointer enters Lua.

See [behavior and UI composition](../../docs/scripting.md). Session frame admission
precedes UI lifecycle callbacks. Lua calls remain outside worker phases.
