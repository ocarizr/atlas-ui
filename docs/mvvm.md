# ViewModels, bindings and authored definitions

`atlas::mvvm` owns reusable observable state and action/event protocols. It links
diagnostics and has no UI, input, renderer, scheduler, editor or storage dependency.
`atlas::mvvm_content` adds XML authoring and C++ factory generation. `atlas::ui_mvvm`
adapts an independent model to UI properties and routed events. Consumers can use
the runtime and installed headers without either adapter.

## Authoring and compilation

A ViewModel is data, not a required user-written C++ subclass. The editable
`definition` contains named fields/defaults, actions and events with ordered typed
parameters. An editor can add/remove fields or signatures by editing this data,
serializing it and compiling a replacement schema. Published schemas are immutable;
changing a definition does not silently mutate running instances.

```xml
<ViewModel version="1" name="hud">
  <Fields>
    <Field name="experience" type="integer" value="0" />
    <Field name="opacity" type="number" value="1" />
    <Field name="visible" type="boolean" value="true" />
    <Field name="title" type="text" value="Status" />
  </Fields>
  <Actions>
    <Action name="gain_experience"><Param name="amount" type="integer" /></Action>
  </Actions>
  <Events><Event name="closed" /></Events>
</ViewModel>
```

Fields, actions and events have separate name namespaces; names are unique within
each namespace. Identifiers use 1–128 ASCII bytes, start with a letter/underscore,
and contain letters, digits, underscores, dots or hyphens. Parameter names are
unique within a signature. Dots are literal name characters, not nested-object
traversal. Current scalar types are boolean, signed 64-bit integer, finite double
and UTF-8 text. Collections, nested models, computed properties and nullable types
are not implemented.

`parse_xml` consumes borrowed UTF-8 bytes provided by the streamer/caller. It never
opens a path. `write_xml` returns canonical UTF-8 for an editor persistence service;
the current streamer remains read-only. Parsing rejects unsupported versions,
unknown/duplicate attributes or groups, unexpected text/elements, DTDs, processing
instructions, malformed Unicode, invalid scalar spellings and exceeded bounds.
Canonical writing preserves declaration order, integer extremes, text escapes and
finite double values, including negative-zero default bits.

```cpp
const auto authored = atlas::mvvm_content::parse_xml(xml_bytes);
const auto schema = atlas::mvvm::compile(authored);
auto model = std::make_shared<atlas::mvvm::view_model>(
    schema, atlas::mvvm::runtime_limits{}, shared_memory, diagnostics);
const auto experience = schema.field("experience"); // Resolve once during wiring.
model->begin_frame(frame);
model->set(experience, std::int64_t{10});             // Demand, not immediate mutation.
model->flush();
const auto current = model->integer(experience);    // Checked indexed column read.
```

Include `atlas/mvvm_content/document.hpp` for authoring and
`atlas/mvvm/view_model.hpp` for runtime-only use. Linking `atlas::mvvm_content` is
unnecessary for a consumer that only calls compiled factories.

## Runtime layout and identity

`compile` copies and validates a definition into a shared immutable schema with
name indexes and per-type offsets. Resolve names to field/action/event handles at
setup. Instances use separate dense boolean-byte, integer, double and PMR-string
columns; a typed read checks owner, schema identity, index and type, then loads its
column. It performs no name lookup, XML interpretation or generic value boxing.
Generic `read` returns an owned variant and copies text; `text` returns a borrowed
view valid until a commit changes that field.

Handles identify one compiled schema in this process. Instances sharing that
schema can use the same handles while retaining independent values. Recompiling
the same authored content creates a new identity; old handles fail validation.
These identities, offsets and native pointers are not serialized asset IDs or a
portable binary-file ABI. Persistent asset identity remains the catalog's concern.

## Actions, events and synchronization

Observers implement `on_notify(notification, view_model&)`. Register against a
field, action or event; action/event signatures validate arguments without implicit
coercion. Immutable schema `validate` can check a protocol during editor/adapter
wiring before a frame exists. Native behavior and the [Lua observer](scripting.md)
use the same interface. Action definitions declare protocols; behavior implementation
belongs to their consumers, independently of editor authoring.

All mutable access, callbacks, subscription cleanup and destruction occur on the
instance owner. Schemas may be read concurrently after safe publication. Workers
must not read a mutable instance or call `set`; pass explicit immutable work inputs
and collect results at a later owner boundary. The model creates no scheduler or
per-property job. Heavy work belongs to joined job phases outside serial dispatch.

`begin_frame` requires an increasing nonzero ID and a drained healthy queue. A
frame may have several explicit `flush` boundaries. Setters queue demands;
callbacks read committed values and append later demands. Recursive flush,
reset/frame reentry and adding subscriptions during dispatch are rejected.
Unsubscription is idempotent and may occur in a callback, immediately suppressing
later deliveries. Reused subscription storage preserves registration order and
does not reuse tokens.

Adjacent pending writes to the same field coalesce to the last value. Distinct
values count as conflicts and produce bounded warning details. Ordered `add_integer`/`add_number` demands preserve every delta and reject overflow
without changing the field. Use these for independent XP/damage-style increments;
reading a committed value and queuing several absolute replacements does not
compose those read/modify/write operations. Signals and relative operations separate
write segments; callback-generated writes cannot rewrite the notification being
delivered. Equal committed values produce no field notification. Every explicit
action invocation/event emission remains ordered, even with identical arguments.
Notification spans are borrowed only for that callback; forwarding them through
`invoke`/`emit` makes an owned queue copy. Rvalue field payloads move into the queue.

Frame-wide command, argument, text-payload and callback budgets bound feedback
cycles. There is no recursive observer traversal or automatic deferral to another
frame. Healthy observers are attempted for an already committed notification even
after another observer fails, within the callback budget. The original failure
propagates, unprocessed demands are discarded and further writes/flushes require
explicit `reset`. Already committed values and arbitrary observer effects are not
rolled back. Retire a faulty observer, reset, then begin a newer frame; exhausted
frame budgets are not reset by recovery. Subscription tokens belong to one model
instance and remain invalid in another instance even with a shared schema.

## UI adapters

`binding_session` retains a model, borrows a live owner tree and owns its field
subscriptions. `binding_definition` stores a source name, target property and
one-way/two-way direction; the caller separately resolves the authored UI element
to a live handle. Names compile once. Model notifications mark matching bindings
dirty, using one observer subscription per distinct source field. No model callback
mutates the UI directly, and weak observer links prevent ownership cycles.

At an explicit boundary, `publish` preflights dirty source values and target
generations, then writes into the tree's open command frame using a caller-reserved
nonzero producer ID. Removed/reused elements retire their bindings. Visibility
requires boolean; width/height/opacity require numeric sources. Integer conversion
must be exact in float; doubles explicitly narrow to UI float, with overflow and
property-domain checks. Strings need future text-widget bindings. Duplicate target
properties in one adapter are errors. Two-way `write_back` queues an explicit model
demand; it rejects implicit truncation, invalid types and automatic-size sentinels.
Equality suppression prevents repeated equal writes from causing an echo loop.

`action_handler` is a generic retained UI input handler. Data descriptors map routed
event kind/stage, or a semantic input action ID/phase, to a named ViewModel action
and typed constant arguments. Setup validates protocols before registering with the
input session. Dispatch only queues matching actions; focus/capture/widget policy
remains in UI. It creates no custom native class per authored ViewModel.

An application can begin the model frame, process UI input/lifecycle events, flush
model demands, publish dirty bindings, seal/apply UI commands and join dependent
layout/paint work. Repeat this sequence at chosen sync points. `input_session`
already opens the tree's command frame; do not begin it a second time. Conversion
failure is detected before any binding command is emitted. Command-buffer failure
can leave partially queued unsealed writes; abort/discard that UI boundary according
to its command contract. UI XML persistence of these descriptors belongs to
[#75](https://github.com/ocarizr/atlas-ui/issues/75). [Lua behavior](scripting.md)
uses this runtime; visual authoring belongs to
[#77](https://github.com/ocarizr/atlas-ui/issues/77).

## Native factories and packaging boundary

`generate_factory_cpp(definition, "make_hud")` returns C++20 source defining
`atlas::generated::make_hud(runtime_limits, shared_memory, diagnostics)`. Compiling
it produces a function returning an owning `std::unique_ptr<view_model>`. The
instance is never copied or moved; callers can promote ownership to `shared_ptr`
when an adapter needs to retain it. It does not call an XML
parser or require XML bytes at runtime. It constructs/indexes a validated schema
once per factory, then creates independent instances with the same runtime layout.
Factory identifiers are constrained; text uses unambiguous byte escapes and double
defaults use exact bit reconstruction. Schema limits are carried into generated
construction. A build test generates a real translation unit from XML, compiles it
and compares the resulting model with interpreted construction.

This implements factory generation, not the complete cooking/linking pipeline or
a portable bundle format. The compiled factory is native executable code; dumping
instance memory would serialize pointers/allocator state and is not a supported
binary format. A future packaging step can compile/link generated factories and
map catalog identities to them, alongside versioned binary content. Rebuilding
native factories does not provide hot reload, cross-version state migration or an
editor implementation by itself.

## Bounds, diagnostics and performance

Defaults allow 4096 fields, 1024 actions and events each, 32 parameters per signal
and 64 KiB per text value. XML defaults to 1 MiB. Runtime defaults reserve 256
commands, 1024 queued argument values, 256 subscriptions, 256 KiB text admission
and 4096 callback attempts per frame. Tune limits for the number of live instances;
the reserved capacities are not free or equivalent to a native struct's footprint.

The injected shared PMR owns instance columns, command/observer metadata and UI
publication scratch. Immutable schema metadata, observer objects, generated schema
setup and owned boundary `std::string` payloads have separate allocation costs.
Text growth can allocate; generic reads copy text. Numeric update tests reject PMR
allocations after setup and verify cleanup. Notifications use direct per-slot
observer chains, avoiding a scan of unrelated model subscriptions.

Profiling covers compilation, instance creation, flush, callbacks, XML/codegen and
UI publication/action mapping. Failures/coalescing conflicts use diagnostics;
typed reads have no per-read logging/profiling. Profiling follows the common
`ATLAS_PROFILING=auto|on|off` policy.

`atlas_mvvm_benchmark` reports cached-slot/name lookup time, numeric update/notify
time and PMR peak bytes for a fixed 64-field workload. `--verify` checks semantics,
bounded memory and cleanup without timing gates. It does not measure whole-process
memory, arbitrary callbacks/text workloads, renderer performance or parity with a
handwritten native struct. Measurements are descriptive and hardware-dependent.
