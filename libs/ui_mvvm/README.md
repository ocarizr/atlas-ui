# UI ViewModel adapters

`atlas::ui_mvvm` adapts independent models to UI properties and routed actions.
Bindings resolve names once, retain safe subscriptions, mark dirty values and
publish through explicit UI command boundaries. UI event descriptors invoke
validated model actions without per-ViewModel C++ subclasses.

See [adapter contracts](../../docs/mvvm.md). XML content, Lua and editor authoring
remain separate consumers; this adapter does not make the MVVM runtime depend on UI.
