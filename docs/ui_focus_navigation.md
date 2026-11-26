# Focus navigation

`ui::input_session` owns event-driven focus navigation using its existing owner,
registered handlers and immutable presented regions. The input library continues
to own physical devices, contexts and semantic actions; it has no UI dependency.
The navigation policy creates no scheduler, jobs or per-request scratch allocation.

## Direction, destination and activation

`focus_direction` has four spatial requests (`left`, `right`, `up`, `down`) and two
logical requests (`next`, `previous`). Spatial search uses the current presented
axis-aligned hulls. Candidates must be live, enabled, visible, registered focusable
and have positive border dimensions. An offscreen but eligible candidate can be
selected and revealed through the existing nested scroll contract. Pointer clipping
and focus eligibility remain separate; painter occlusion is not automatic disabling.

For a spatial request, candidate centers must lie in the requested half-plane.
Prefer candidates whose perpendicular intervals overlap the origin's beam, then
minimum squared edge-gap distance, perpendicular-center distance, forward-center
distance and stable input registration order. Double arithmetic avoids overflow
for finite float hulls. Automatic choice is deterministic for matching inputs; it
does not infer design intent. Authors override non-geometric relationships explicitly.

`focus_navigation_options::destinations` uses the direction enum's fixed order.
An empty entry means automatic search; `blocked` disables that direction. A block
cannot also have a target. An explicit target wins over automatic search and can
bridge groups inside the same input session. If it becomes stale, hidden, disabled,
unbound or empty-sized, the request stays put and optionally logs a warning. It does
not silently choose another control. Document-local authored IDs resolve to full
session/slot/generation handles during private instance construction.

Ordered traversal sorts `tab_order` ascending and breaks ties by stable registration
identity (the captured eligible input table's order, independent of paint Z order).
`tab_stop=false` excludes a control from ordered traversal while retaining spatial
and programmatic focus. With no eligible origin, ordered traversal enters its first
or last eligible item; spatial traversal enters the first eligible registered item.

Moving focus emits the normal focus-lost/focus-gained and ensure-visible events.
It never synthesizes click, confirm or an application action. Ordinary buttons still
need activation. Tabs may bind their existing `focus-enter` event to an authored
Flow navigation action to select/load a submenu. That behavior belongs to the tab's
authored event binding, not to the geometric focus selector.

## Scopes, mappings and wrap

Automatic traversal stays in the nearest configured scope, otherwise the independent
root containing current focus. Nested scopes are separate automatic groups. Explicit
connections may bridge them; separate Flow input sessions still isolate views/popups.
When no origin exists, a request can establish focus in the first eligible root/group.

Wrapping is off by default. A scope can enable horizontal, vertical or ordered wrapping
independently. A spatial wrap chooses the opposite edge, retaining beam preference and
stable ties; ordered wrap cycles its endpoints. No other direction is implicitly added.

```cpp
ui::focus_navigation_scope tabs;
tabs.actions.previous = previous_tab_action;
tabs.actions.next = next_tab_action;
tabs.wrap_tab = true;
input.configure_navigation_scope(tab_row, tabs);
input.configure_navigation({}, true); // Opt into arrows and Tab/Shift+Tab.
```

Scope-targeted semantic actions can operate from elsewhere in the view, using that
group's last eligible focused member. Without such a member, the first request enters
the group. This history is focus history, not an inferred selected-tab model value.
The host establishes initial tab focus/selection through its normal lifecycle/Flow
behavior. Nonzero focus action IDs must be unique across the view's global/group
navigation mappings. Configure input contexts to map D-pad/arrows, LB/RB or other
controls to those semantic IDs; navigation never hardcodes physical gamepad buttons.

Only performed semantic actions request movement. Repeats, analog thresholds and
context priority belong to the input mapping layer. Portable key replay uses Shift
state for reverse Tab; blur, suspension and failed frames clear that state. Default
keyboard handling is opt-in and does not change existing views without configuration.

User capture/target/bubble handlers can consume packets before built-in navigation.
The legacy ordered scroll policy yields only for recognized new navigation packets.
Virtual-list handlers retain their existing logical offscreen focus/recycling policy;
generic automatic traversal itself only sees currently registered participants.

## Authoring

```xml
<ui version="1">
  <navigation-input keyboard="true" right-action="81"/>
  <element id="root" panel="horizontal">
    <focus-scope wrap-horizontal="true"/>
  </element>
  <element id="first" parent="root">
    <layout intrinsic-width="20" intrinsic-height="20"/>
    <interaction focusable="true"/>
    <focus right-target="third" tab-order="0"/>
  </element>
  <element id="second" parent="root">
    <layout intrinsic-width="20" intrinsic-height="20"/>
    <interaction focusable="true"/>
  </element>
  <element id="third" parent="root">
    <layout intrinsic-width="20" intrinsic-height="20"/>
    <interaction focusable="true"/>
  </element>
</ui>
```

`navigation-input` is optional view configuration: six `*-action` IDs plus `keyboard`.
`focus-scope` accepts the same six semantic IDs and `wrap-horizontal`, `wrap-vertical`,
`wrap-tab`. `focus` accepts six `*-target` IDs, six `*-blocked` booleans, `tab-order`
and `tab-stop`. Authored own/explicit destination metadata requires focusable
interaction declarations. Missing references, conflicting block/target pairs,
ambiguous action IDs, duplicate sections and unknown fields fail validation before
publishing a runtime instance. Runtime eligibility remains dynamic after construction.

Prepared UI version 8 appends optional navigation data; versions 2–7 remain readable.
Documents without this metadata keep their previous version-selection rules. Native,
XML and prepared construction use the same runtime configuration. Editor visual
restyling and new navigation inspector UX are not supplied by these descriptors.

## Boundaries, diagnostics and measurement

Configure mappings, overrides and scopes between frames. Programmatic `navigate`
runs in an open owner frame, uses the same callback/reentry/failure guards as input
packets and returns whether the requested focus was established. Structural and
geometry changes are observed at the next coherent input boundary. Stale handles
never select a recycled generation. Unbinding clears that registration's overrides.

The existing binding budget bounds sparse node/scope records. Candidate visits across
a frame are additionally bounded by `max_navigation_candidates` (default 1,048,576,
maximum 16,777,216), reported by `input_statistics::navigation_candidates`. Exceeding
admission fails the frame before choosing a new candidate; existing failure cleanup
discards unsealed commands. Earlier callback side effects are not rolled back.

`ui.input.navigate` profiles selection and transitions; the programmatic
`ui.input.navigate.request` scope also includes final reconciliation. Both are
optional owner-thread profiling probes; unavailable explicit
targets use the `ui.navigation` warning category. Global profiling AUTO/ON/OFF remains
authoritative. The benchmark `atlas_ui_benchmark --focus` compares automatic and
explicit requests over 64/1,024 focusable grid elements after setup, verifies the same
focus sequence and separately checks two warmed requests allocate no C++ storage
with no-op handlers and absent capture destinations. `--focus-smoke` checks correctness
without a timing threshold. Timing includes focus notifications/reveal but excludes
layout preparation, parsing, user callback work, storage and GPU work. Allocation
counters are not whole-process memory or fragmentation measurements.

Accessibility/platform assistive technology and drag-and-drop have separate contracts;
focus movement alone does not provide either capability or hardware gamepad support.
