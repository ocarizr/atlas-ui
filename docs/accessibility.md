# Accessibility

`atlas::accessibility` is an independent semantic publication/request library. It has no UI,
editor, Lua, renderer, storage or Windows SDK dependency. `atlas::ui` supplies the owner bridge;
`atlas::accessibility_host` attaches the selected platform adapter to an existing window.
Windows Desktop uses server-side UI Automation. The editor and runtime composition roots attach
the adapter to their native window and publish their active views. Other applications can reuse
the same adapter and owner-frame bridge without linking editor code. Other platform builds report the native
adapter unavailable. Native headers and COM implementations remain private.

## Ownership and frame integration

A channel has one publication/drain owner. Native queries can read retained immutable snapshots
and submit bounded value-only requests concurrently. They cannot execute application callbacks,
read EnTT, call Lua, resolve localization, perform storage I/O or block a CPU job on the UI owner.
Successful request admission means queued, not completed. The owner revalidates the target and
state before executing it during the input phase.

```cpp
auto channel = std::make_shared<atlas::accessibility::channel>();
atlas::accessibility_host::adapter host{window, channel, diagnostics};
// Destroy host before window. Publish only when input has a successful open frame.
const auto screen = host.coordinates();
atlas::ui::accessibility_view view{screen.x, screen.y, screen.scale, active_modal};
channel->publish(input.accessibility_nodes(view, resolve_accessible_text));
const auto requests = channel->take_requests();
input.process_accessibility(requests, active_modal);
// Normal input processing also occurs in this frame, before tick/late_tick/end_frame.
channel->publish(input.accessibility_nodes(view, resolve_accessible_text));
host.notify();
// On owner shutdown, cancel pending requests and disconnect before releasing the window.
channel->close();
```

The application supplies the active modal subtree and client-to-screen transform. Refresh the
transform after window movement or DPI changes. Bounds are physical screen pixels and permit
negative monitor coordinates. Captured UI geometry includes the input/paint transforms and
ancestor clipping. Focus, selection and activation are independent states/actions.

A focus request uses existing input focus and scroll reveal. Invoke/select requests deliver an
existing performed semantic input action to the target's normal route; a button without an input mapping uses the device-independent `activate` event. They do not manufacture
pointer presses or call Lua outside normal serial dispatch. Exposed actions require matching
authored/native handlers. An unsupported or stale request has no application side effects.

## Semantic data

Elements are generic layout/paint primitives. Focusability alone does not identify a button.
Known authored click events, target-phase MVVM `activate` bindings and captions can supply
button/text defaults; explicit metadata
overrides those defaults. Purely decorative images need no descriptor. Omitting a decorative
container reparents exposed descendants to their nearest semantic ancestor; hiding a subtree
removes its descendants. Disabled controls remain readable and reject actions.

Names and descriptions can use literals or localization keys, never both for the same field.
The application resolver runs only during owner publication; native readers receive owned
resolved UTF-8 strings. Republishing after language changes produces updated text notifications.
Selection/checked/value state comes from application/MVVM state; keyboard focus does not set
selection or toggle a checkbox. Sliders expose read-only numeric ranges in this increment.

XML metadata, prepared UI v9 and native construction share the descriptor contract; older
prepared UI versions remain readable. A semantic activation ID is an input-context action,
not a physical gamepad button. Custom native/Lua handlers can handle `activate`; authored
`click` events already share that route. Visual captions and accessible names remain independent.

```xml
<element id="apply" parent="settings" text="Apply">
  <interaction focusable="true"/>
  <accessibility role="button" name-key="settings.apply"
                 description-key="settings.apply.help"/>
  <event-action event="click" action="atlas.navigate_to"
                screen="settings" route="accept"/>
</element>
```

An element can set `decorative="true"` to suppress inferred semantics. It cannot also declare
an explicit accessibility section. That exclusion does not hide its meaningful descendants.
The editor inspector exposes an **Accessibility** section for the selected element. Choose
inferred, explicit or decorative semantics. Explicit descriptors include role, literal/localized
name and description, read-only value/range, semantic action IDs, selection and checked state.
Field edits remain private until Enter confirms the field and **Apply** validates the complete
descriptor as one undo transaction. Escape cancels the field. Changing selection or document
revision discards stale drafts. Assigning a literal clears its localization key and vice versa.
The paginated inspector uses a bounded single-line UTF-8 edit buffer (1,024 bytes); committed
Unicode text comes from the input backend rather than reconstructed physical key codes.
It does not provide caret movement, selection, clipboard commands or UIA editable-text patterns.


## Window and view composition

`ui::accessibility_frame` drains a window channel once, routes queued requests through open
input sessions, and captures owned semantic contributions before those phases close. It preserves
session/generation identities across independent trees and publishes only changed values. The
window owner carries the reported keyboard-domain identity into the following frame; native
queries never retain these input sessions. At most 256 active-view contributions are admitted.

The editor includes native chrome and currently visible authored tools. An authoring canvas is
an inert visual preview, so it does not advertise executable controls. An explicitly running Flow
preview supplies active menu/submenu/popup semantics through `flow_frame_options::accessibility`.
Inactive/hidden Flow domains do not contribute nodes; modal policy follows the existing Flow
input ownership. Replaced chrome is recaptured before publication; retired tool contributions
are omitted until their next successful owner frame. Minimizing the window clears its publication.
Shutdown closes the channel before disconnecting the adapter and destroying the window.

Application resolvers are passed to `editor_view::bind_accessibility` and Flow frame options.
The editor's default resolver presents a missing localization service's key as readable fallback
text; an application with language tables supplies its resolver and republishes after language
changes. Resolved strings are never looked up on native query threads.

These small serial publication and request batches use existing diagnostic scopes. They do not
create workers or per-query jobs. Comparing snapshots avoids redundant native notifications;
it does not eliminate the cost of owner capture itself.

## Bounds and lifetime

The channel bounds node count, total text bytes and pending requests. Full identities contain
session, slot and generation. Removed/recycled nodes cannot reuse an old provider or queued
request. Invalid hierarchy, malformed UTF-8, numeric geometry/ranges or publication budget
failure preserve the prior snapshot. Retained snapshots own their strings and remain readable
after owner disconnection; retained providers then report element unavailable.

Virtualized lists expose their realized semantic nodes. The adapter does not allocate a widget
or provider for every logical row. Generic UIA virtual-item discovery/realization is not supported.
The host retains its previous notified snapshot; external clients can retain independent values.
Those external lifetimes are not a whole-process memory cap. String and COM allocations are
not covered by the UI descriptor vector's PMR resource.

## Publication measurement

`atlas_ui_benchmark --accessibility` forces full owner capture, validation and channel publication
for 64/1,024 controls with owned names longer than SSO, using eight warmups and 100 samples.
A separate untimed C++ allocation census reports request counts and cumulative requested bytes
for one full publication. These counters are not retained/peak/RSS memory, allocator caches or
fragmentation evidence. The timing excludes startup/layout, native COM queries, OS notification,
Narrator and application handlers. No publication jobs are admitted. Full publication has a
linear payload and transient index/string costs; publishing only when presented semantic state
changes avoids unnecessary work. The application owns that publication policy.

## Diagnostics and validation

Owner publication, capture, request dispatch and native notifications have optional profiling
scopes controlled by the existing AUTO/ON/OFF diagnostics build policy. Setup/shutdown and
missing actionable names use optional logs. No private scheduler or per-query job is created.

Portable tests cover publication rollback, queue capacity, ownership, stale identities and
shutdown. UI tests cover geometry, localization, disabled/removed controls, semantic activation
and modal restrictions. Windows fixtures test native properties, patterns, fragment navigation
and disconnect behavior without requiring GPU rendering or a running screen reader.

These automated provider tests are not a claim of verified Narrator/NVDA compatibility. The
Narrator acceptance walkthrough must record its Windows/reader version and results for reading
names/help/value/state, directional focus, invoke, tab selection, modal popups, reveal, language
changes, recycled entries and shutdown. Full editable-text selection/caret support and NVDA
compatibility are outside the current menu-focused adapter contract.

The editor restores a focused native command after rebuilding chrome, while retiring the old
runtime identity. Repeated reconstruction and virtual-list reader navigation still require the
interactive Narrator acceptance described above. Automated tests and tree inspection do not
establish the reader's speech, traversal or announcement behavior.
