# Input

`atlas::input` provides portable device events, bounded owner ingress, semantic
actions, priority contexts, runtime bindings, modifiers and hold/tap interactions.
It links diagnostics and has no UI, window, renderer or jobs dependency.

See [input contracts and examples](../../docs/input.md). Windows normalization is
private platform code; SDK-free builds retain the action mapper and replay tests.
Physical gamepad support is a future adapter, not a current supported device.
