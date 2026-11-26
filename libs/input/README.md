# Input

`atlas::input` provides portable device events, bounded owner ingress, semantic
actions, priority contexts, runtime bindings, modifiers and hold/tap interactions.
It links diagnostics and has no UI, window, renderer or jobs dependency.

See [input contracts and examples](../../docs/input.md). Windows normalization is
private platform code; SDK-free builds retain the action mapper and replay tests.
Physical gamepad support is a future adapter, not a current supported device.


Committed `text_input` packets carry one Unicode scalar in `event::control`. They are raw text,
not held controls or action bindings. The Windows adapter combines UTF-16 `WM_CHAR` surrogate
pairs and discards incomplete pairs on focus loss. `text_entry` provides bounded single-line
UTF-8 drafts with scalar-safe backspace; consumers own confirmation, cancellation and focus.
IME composition UI, caret selection and clipboard behavior are separate capabilities.
