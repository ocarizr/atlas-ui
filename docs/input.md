# Input actions and device adapters

`atlas::input` is a reusable owner-thread library. It depends on diagnostics and
does not depend on a window, UI tree, renderer, scheduler or application. Its
public headers contain no native SDK types. Windows message normalization lives
under `libs/platform/windows/input/` and is selected by the existing CMake
platform/integration flags. The window forwards native messages to that adapter;
it does not own action mappings or dispatch application listeners.

## Actions, contexts and bindings

An **action** expresses intent, such as confirm or navigate. It has a stable
nonzero ID and a button, scalar or two-dimensional value. A **context** is a
priority-ordered mapping of physical controls to actions. A **binding** selects a
device family/control and optionally one device instance or event source.
Configuration and diagnostic names are copied; immutable processors are retained.

```cpp
atlas::input::input_system input{{}, shared_memory, diagnostics};
input.define_action({1, "confirm"});
const std::array bindings{
    atlas::input::binding{1, {atlas::input::device_kind::keyboard,
        static_cast<std::uint32_t>(atlas::input::key::enter)}},
    atlas::input::binding{1, {atlas::input::device_kind::gamepad, 1}}};
input.define_context({1, "menu", 10, true,
    atlas::input::activation_policy::require_neutral, bindings});
// listener derives from input_listener; no listener is retained after dispatch.
input.dispatch(frame, monotonic_seconds, ready_events, listener);
```

Include `atlas/input/input_system.hpp` and `atlas/input/key.hpp`, and link
`atlas::input`. Gamepad control codes in this example are synthetic backend
identifiers; a physical gamepad backend is not implemented.

Contexts run by descending priority, then ascending ID. Consuming bindings claim
matching controls against lower contexts; nonconsuming bindings permit sharing.
Within a context, several bindings may contribute to one action. For a given
action, its highest enabled mapped context owns its bindings even when neutral.
Buttons combine by maximum absolute magnitude and a 0.5 threshold; axes sum in
stable mapping/control order. `axis_modifier` supports directional key composites;
scale, radial deadzone, normalization and a bounded modifier chain are provided.
Custom immutable modifiers/triggers execute on the owner and must not wait.

`request_context` coalesces enable demands until the next dispatch and is the only
mutation permitted from a listener callback. Conflicting demands log a warning;
the last demand wins. `rebind` replaces a context between dispatches. Its new
interaction generation applies at the next dispatch. Preemption cancels an active
interaction. By default, a control held before activation must become neutral
before it can actuate the new map. `allow_held` explicitly opts out.

Actions emit started, ongoing, performed, completed and cancelled transitions.
Ordinary buttons perform on actuation; value changes can perform again. Hold and
tap use the caller's monotonic clock, including empty dispatches, without sleeping
or scheduling timer jobs. Ongoing is emitted at most once per action per dispatch.
Normal recognized release completes; reset, preemption, disconnection and failed
recognition cancel. One disconnected device does not cancel an action still held
by another matching device. Tap expiry stays rejected until neutral.

## Ready events and ownership

`event_queue` is an owner-affine fixed 256-packet ingress buffer. Publication and
draining do not wait, allocate event storage, or invoke application callbacks.
Each queue has a unique nonzero source ID. Device IDs must remain stable within
that source; a family change requires disconnection. Overflow replaces the batch
with one source reset and ignores more packets until drain, preventing a lost
release from leaving actions latched. Source zero in a reset explicitly resets
all sources. Source/device selectors let callers isolate mappings.

`input_backend::pump` is the extension contract for collecting ready backend
events. It runs on its owner, without waiting or dispatching UI callbacks. A
future SDK that delivers callbacks on other threads requires a separately bounded
ingress adapter. This owner queue is not a cross-thread producer queue.

The Win32 adapter currently collects primary-pointer movement/button/cancellation,
focus loss and basic keyboard keys. Pointer-button position is auxiliary packet
data; its action value remains zero/one. Coordinates are signed client pixels.
This is not text/IME, wheel, touch, multi-pointer, chord recognition, device-user
pairing, persisted XML maps, a rebinding editor or physical gamepad support.

The application pumps/drains ready events and explicitly calls `dispatch`, even
for an empty frame. Raw packet callbacks precede that packet's action transitions.
The event span must remain alive and unchanged for the whole dispatch.
Listener delivery is synchronous and owner-affine. There is no input worker, hidden
polling thread or wait for new samples. Arbitrary client callbacks can still do
expensive work; this API does not promise wait-free execution or a latency bound.
Independent work belongs to later joined job phases, outside Lua/UI dispatch.

## UI integration and limits

The UI library consumes semantic action values and raw pointer/key replay packets.
It owns focus, capture, hit testing, lifecycle and hierarchy propagation; these
policies are not part of physical input. Semantic actions route to current focus
with their original action ID/value/phase. The shared game/editor demonstration
maps Enter/Space to confirm and Escape to cancel using the same composition.
See [UI interaction](ui_input.md) for frame and command publication boundaries.

Capacities bound actions, maps, bindings, devices, retained controls, incoming
events and callback attempts. PMR vectors and admission scratch reserve capacity
at construction. Configuration/rebinding, processor ownership and application
listeners have separate allocation costs; these are not process-footprint claims.
Whole-batch syntax/device/capacity preflight precedes callbacks and frame admission.
Invalid frame/time or preflight failure permits retry of the same frame. After
admission, listener/processor/budget failure neutralizes state and latches failure;
explicit `reset()` is required. Client side effects are not rolled back and reset
does not invoke observers. Destruction must occur on the owner outside callbacks.

Optional profiling covers dispatch; failed dispatches and conflicting context
requests log warnings through diagnostics. Profiling follows
`ATLAS_PROFILING=auto|on|off`. No per-sample logging or jobs are added.

## Design references and gamepad direction

The action/map/binding vocabulary follows the separation of intent and devices in
[Unity Input System](https://github.com/Unity-Technologies/InputSystem/blob/develop/Packages/com.unity.inputsystem/Documentation~/using-actions-workflow.md)
and [Unreal Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine).
This library implements a bounded foundation, not full feature parity.

[SDL3 gamepad](https://wiki.libsdl.org/SDL3/CategoryGamepad) is the recommended
candidate for a future adapter because it provides standardized controls and
extensible mappings with public source under the
[zlib license](https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt).
Its event pump must run on the main thread. The adapter must coexist with the
existing Win32 window and D3D12 renderer. SDK pinning, virtual-device/hotplug tests
and physical integration are tracked in
[#132](https://github.com/ocarizr/atlas-ui/issues/132); no SDL dependency is installed
by this increment. Optional mapping-file bytes must come through the streamer.
