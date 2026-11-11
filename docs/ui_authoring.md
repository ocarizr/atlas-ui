# UI authoring documents

`atlas::ui_content` reads and writes viewport UI XML v1 and constructs an
owner-thread UI instance from a typed asset catalog. `parse_xml` returns
editable data. `instance::build` validates the native layout rules and
compiles bindings and behaviors into a private candidate. `write_xml` returns
canonical UTF-8 bytes for the host's persistence boundary.

```xml
<ui version="1">
  <model name="hud" asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"/>
  <element id="root" panel="canvas"/>
  <element id="status" parent="root" panel="leaf" width="0">
    <canvas-slot x-offset-min="12" y-offset-min="12"/>
    <paint fill="true" r="0.1" g="0.2" b="0.4"/>
    <bind model="hud" field="enabled" property="visible" mode="one-way"/>
    <action model="hud" name="gain" event="pointer-up">
      <arg type="integer" value="17"/>
    </action>
    <behavior model="hud" asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000002"/>
  </element>
</ui>
```

## Identity and validation

Each document has one root. Declare parents before children. Declaration
order preserves sibling order. Element IDs and context names are
case-sensitive and use 1–64 ASCII letters, digits, underscores or hyphens.
`find` resolves a stable authored ID into a live handle for the current
instance. A replacement has new handles and keeps the authored names.

A document can contain up to 4 MiB of XML, 4,096 elements, 64 named models
and 64 levels of ancestry. Parsing rejects unknown fields, malformed values,
unsupported versions, duplicate IDs and missing local references. Syntax
errors identify the source and byte offset. Construction checks property
ranges, panel arity, slot ownership and sibling boundary cycles using the
native UI validators.

| Location | V1 data |
| --- | --- |
| Element | Overlay, leaf, size box, horizontal or vertical stack, or canvas. An omitted width or height is automatic. An explicit zero is fixed. |
| `layout` | Intrinsic size, margins, padding, min/max and alignment. An omitted maximum is unbounded. |
| `paint` | Rectangle fill, clip, layer, RGBA, UV, translation, transform origin and optional persistent texture. |
| `canvas-slot` or `stack-slot` | Anchors, pivots, offsets and sizing, or child order and grow/shrink weights. |
| `horizontal-boundary` or `vertical-boundary` | Stable sibling source, side, gap, sizing and overflow. |
| `interaction` | Enabled, hit-test and focusable flags. |
| `bind` | Model, field, property and one-way or explicit two-way mode. One binding per target property. |
| `action` | Model and action, event, route, semantic input filter where applicable, and exact scalar arguments. |
| `behavior` | Script asset and model context. One returned handler table per element in the shared Lua domain. |

Model, script and texture use sites fix the expected
[asset type](assets.md#persistent-identity). The same references resolve
against loose files or prepared backing ranges. The host receives the
persistent texture reference for image residency.

## Construction and frames

`load` reads an authored UI from the streamer. The same constructor accepts
a C++ `document` from a prepared source. Bootstrap reads wait on the owner
during explicit construction. Streamer jobs deliver buffers and the owner
checks their mount stamps before use. Each named context receives its own
ViewModel. A host can supply a [native factory](mvvm.md) for the matching
asset revision. Both paths compile the same bindings and actions. The UI
Lua domain registers each script source once and creates an isolated handler
table per element.

Bootstrap reserves command frame 1 for initial scalar values. Begin owner
frames at 2 or a larger host-selected frame number. Call `begin_models`
before the matching input frame. `synchronize` flushes models in authored
order, applies pending Lua subscription changes, and queues model-to-UI
writes into the open command frame. The host seals and applies those
commands and controls Lua work/result delivery. `write_back` queues an
explicit reverse update. Input dispatch enqueues matching MVVM actions
in declaration order before calling the element's Lua handler.

Build the replacement before retiring the active instance. To deliver
lifecycle notifications at retirement, begin a newer matching model/Lua
frame, shut down input, and synchronize. Destruction releases registrations,
subscriptions and pending Lua work on the owner.
