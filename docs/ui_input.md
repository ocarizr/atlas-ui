# UI input and lifecycle

`atlas::ui::input_session` owns serial interaction for one live `element_tree`.
It borrows the tree and shares its sole command producer. The application retains
both on the same coordinator; no worker invokes input/Lua/session callbacks.
Handlers are retained interfaces suitable for native behavior and a future Lua
adapter. This implements routing and lifecycle, without a Lua VM or widget set.

## Frame boundaries

Bind an element to a shared `input_handler` between successful frames. Sparse
`interaction_options` control enabled state, own-border hit testing and focus
eligibility. Disabling a registered ancestor disables its descendants. Unbound
ancestors need no handler or per-widget job. `configure`/`unbind` are boundary
operations; callback reentry is rejected, including observations.
An aborted/failed frame is also a quiescent configuration boundary, so a faulty
handler can be retired before beginning a recovery frame.

```cpp
atlas::ui::input_session input{tree, {}, shared_memory, diagnostics};
input.bind(button, handler, {.focusable = true});

const auto source = tree.snapshot();
const auto geometry = layout.calculate(source); // Existing shared jobs policy.
input.begin_frame(42, source, geometry, delta_seconds);
input.process(replay_events); // Also call for an empty input batch.
// Seal/apply demands and run/join independent work at chosen boundaries.
input.tick();
// Another caller-owned sync point can follow tick.
input.late_tick();
input.end_frame();
tree.apply(tree.commands().seal());
```

Frame IDs are positive and increasing; delta is finite and nonnegative. Prepare
matching source/layout session, revision, viewport and element identities. Use
the same committed presentation boundary for painting and hit testing. The
geometry revision does not version appearance; callers must supply the intended
visibility, translations and clips, rather than assuming a revision detects every
appearance edit. Geometry is frozen for this input frame. Publish a new boundary
after geometry-affecting commands before using them for later hit testing.

`begin_frame` opens the tree's command frame. Do not call the producer's
`begin_frame` again or run a second independent input producer for that tree.
Use the existing `frame_executor` for named serial/job phases around these calls;
input creates no scheduler or implicit work phase. Callback commands become
visible only when the caller seals/applies them. Prior sealed publications and
arbitrary callback side effects are not rolled back by later failure.

| Stage | Notifications |
| --- | --- |
| Begin | Reconcile stale ownership; first active `start`, then `enable` |
| Process | Active `process_input`, then validated packets in caller order |
| Tick | Active `tick` |
| Late tick | Active `late_tick` |
| End frame | Active `end_frame`; close the serial frame |
| Explicit shutdown | Clear ownership; attempt `disable`/`end` in a new terminal command frame |

Start is attempted once at first enabled participation, not at registration of an
inactive element. Enable/disable follow effective state transitions. End is
attempted once for a started removed/unbound handler or explicit shutdown.
Initially inactive handlers that never started require no end callback. Begin
starts parents before children in resolved presentation traversal; system broadcasts
and reconciliation use stable slot/generation registration order. Cleanup follows
the current serial stage/packet, not a deferred worker callback.

## Hit testing and routes

Input preparation shares painting's stacking traversal, inherited translation,
explicit visibility and rectangular clips. Ordinary paint calculation does not
allocate input-region output. Input optionally captures borders for **all** nodes,
including panels without a fill. Image contain/cover changes the background,
not its border hit shape. Transparent/zero-opacity elements can remain interactive;
use explicit visibility, enabled state or hit-test policy to suppress interaction.

Hit testing walks front-to-back isolated painter order. Both the translated border
and effective ancestor clip must contain the point; right/bottom edges are excluded.
An unbound or own-hit-test-disabled panel does not intercept descendants. Child
layout order does not replace painter stacking order. Empty clips cannot receive
pointer hits. Existing arbitrary shape/alpha masks are not available.

Raw pointer/key routes visit registered ancestors root-to-parent (`capture`), the
destination once (`target`), then parent-to-root (`bubble`). `event_response::stop`
stops the route after that handler. Events include original/current identity,
viewport/local coordinates, translated target/current borders and effective clip.
Live generation/visibility/ancestry checks precede later deliveries. Removal in an
earlier callback prevents stale target input; reused slots cannot inherit handlers.
Reparented paths wait for a coherent new presentation boundary.

## Capture, focus and reveal

A pointer-down response can capture the primary pointer to the current receiver.
Motion/up route there even outside its border/clip. Up releases capture after
routing; cancellation releases capture without clearing keyboard focus. Hover
remains a separate geometric target with direct enter/leave notifications.
Activation loss clears capture, hover and focus. Removed/hidden/disabled targets
lose ownership before further eligible dispatch. Ownership is reset before a
possibly throwing lost/leave notification.

Focus is explicit through a focusable handler response or `focus(target)` during
an open frame. Empty clears focus; an invalid request preserves the previous
focus. Key packets and semantic `atlas::input::action_event` packets route to focus.
Action payloads preserve intent, value, phase and dispatch identity. A focus transition produces direct lost/gained
notifications and an ancestor `ensure_visible` request containing frozen target
geometry. An ancestor can enqueue a reveal command and stop that request.
Off-clip enabled targets may receive programmatic focus so a scroll container
can reveal them. This hook does not implement scrolling or navigation policy.

Wheel/scrollbars and first/last reveal are tracked in
[#121](https://github.com/ocarizr/atlas-ui/issues/121); virtualization in
[#122](https://github.com/ocarizr/atlas-ui/issues/122); keyboard/gamepad navigation,
accessibility and extended input in [#98](https://github.com/ocarizr/atlas-ui/issues/98).

## Failure, cleanup and limits

Whole-batch preflight rejects invalid event kinds, nonfinite coordinates and packet
overflow before process/raw callbacks. Preparation/identity/allocation failure
before admission preserves the previous frame and allows retry with the same ID.
After admission, callback errors, invalid responses, local-coordinate overflow or
callback-budget exhaustion latch failure, discard unsealed writes and clear input
ownership. The original exception propagates; only a newer begin or shutdown can
continue. Attempted lifecycle notifications are not automatically retried.

`process` borrows the packet span synchronously. Keep its storage and values stable
through all callbacks; copy `routed_event` values when retaining observations.

Direct removal/hide is safe for dispatch identity checks. A command queued for an
element later removed must still satisfy the tree's all-or-nothing publication
validation; stale command targets are not silently retargeted or filtered. The
application owns publication failure handling.

Explicit `shutdown(new_frame)` attempts all retained lifecycle cleanup even when a
handler fails, then rethrows the first failure. It is terminal and idempotent.
Seal/apply or deliberately discard final commands before releasing the tree.
Destruction runs no callbacks and must occur on the owner outside dispatch.
Handler-owned external resources still require RAII.

Defaults are 65,536 presentation nodes, 1,024 registrations, 256 packets, ancestry
depth 64 and 65,536 callback attempts per frame. Registered route depth and
metadata admission are checked before frame publication. Shared PMR retains
input metadata; handler allocations, commands, tree snapshots and scheduler memory
are separate. Reconciliation scans bindings at stage/packet boundaries rather
than after every system callback. Preparation currently rebuilds ordering and
region metadata; no low-footprint or latency benchmark is claimed for this layer.

Optional profiling scopes cover prepare, process, system stages, callbacks and
shutdown; frame failures log warnings. Profiling follows the common
`ATLAS_PROFILING=auto|on|off` policy. Local tests cover replay, clips/stacking,
capture/focus cleanup, lifecycle ordering, stale generations, allocator rejection,
worker/reentrant misuse and best-effort shutdown.

## Native transport

The Windows adapter only normalizes/enqueues primary-pointer and basic key values
inside WNDPROC. `window::take_input()` drains at the owner after message polling;
presentation leases do not expose input. The 256-entry queue allocates no callback
storage. Overflow discards incomplete transitions, publishes one `source_reset`
reset and ignores further packets until drain. The host logs this condition.

Coordinates are signed physical client pixels. Semantic keys include ASCII
letters/digits and explicit navigation/control identifiers; native SDK constants
stay private. Repeated key messages retain message order. This is not text/IME,
modifier/chord, wheel, touch, multi-pointer, gamepad or accessibility support.
Hosts map packets into the portable UI API. The demonstration uses one logical UI
unit per physical pixel consistently for viewport and input, retains main-thread
handlers, coalesces highlight demands at late tick and shares the same backend
between game/editor. Native tests replay messages into isolated hidden windows.

## Reusable device input

[Input actions](input.md) belong to `atlas::input`, independent of UI and window.
The window drains generic `input::event_batch` values; the application interleaves
normalized raw packets and semantic actions for UI routing. UI retains element
focus/capture/lifecycle. The shared demo maps confirm/cancel in an input context,
then routes those actions to focused handlers without device-specific behavior.
