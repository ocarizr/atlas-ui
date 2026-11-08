# Shared Lua domains

`atlas::scripting::domain` owns one coordinator-thread interpreter for many
logical behavior instances. Create separate UI and application domains when their
lifetimes differ. Closing a menu removes its UI instance; the application domain
continues to receive its own explicit updates. Domains contain no game-specific
simulation or world implementation and do not create OS threads or schedulers.

The Lua director in `libs/scripting/src/director.lua` is a stateless routing
function embedded at build time. Native code validates identity, activation,
frame, budgets and deterministic registration order before it enters the director.
The director selects the target's named handler and forwards its scalar arguments;
behavior stays in the instance's returned handler table.

## Composition

```cpp
auto model = std::make_shared<atlas::mvvm::view_model>(schema);
auto ui = std::make_shared<atlas::scripting::domain>(limits, memory, diagnostics);
ui->bind_context("hud", model);
ui->register_source({"menu.behavior", 1, "@/project/scripts/menu.lua", source_bytes});
auto menu = ui->create({"menu.behavior", "hud"});

model->begin_frame(frame);
ui->begin_frame(frame);
ui->call(menu, "enable");
ui->synchronize();
ui->dispatch("tick", tick_arguments);
model->flush();
ui->synchronize();
```

Each instance selects one named ViewModel context as its `model` capability.
Different instances may bind the same model or different models within one
interpreter. Model schemas remain data-defined; no native subclass is required.
An instance's globals, public standard-library tables, module cache, handler
locals, listener edits and native-work mailbox are private. The interpreter's
Lua allocation budget is shared across all its instances. Instance observations
therefore alias the same physical Lua byte counters; do not sum them. Use domain
statistics for aggregate interpreter memory.

Transient command/request/listener buffers are reused by the domain's serial
executor rather than reserved per widget. Instance mailboxes reserve pending-work
slots only when native work is actually admitted. Logical instances still retain
their own committed state, listener edits, cancellation and result queues.

The coordinator admits each used model's healthy increasing frame before the
domain frame. An enabled failed instance requires reload or disable before the
next domain frame. Disabled contexts need not advance while presentation is
inactive. Activation requires the model's current healthy frame. New instances
created during an admitted frame must also have a matching model frame.

`dispatch` visits enabled instances in registration order. `call` targets one
exact instance and optionally validates a result-type protocol. A failure stops
that dispatch; earlier successful instances remain committed producers. There
is no cross-instance or cross-model transaction. Each protected behavior call
retains the session's atomic model/native/listener admission and original native
error propagation. The failed instance requires explicit recovery.

## Catalog sources and modules

The caller supplies `source_asset` identity, nonzero revision, source/chunk name
and owned text bytes. The streamer remains the application-managed storage
boundary. The domain never resolves a file path or opens an asset. Register
source/module bytes only after the external catalog/streamer has delivered them.
Source identities are exact, case-sensitive catalog keys, not filesystem lookups.

Publishing a source requires a strictly newer revision for its existing identity.
Publication does not reload live instances. Each creation/reload captures an
immutable source and service catalog snapshot. Catalog metadata is copied on
publication and shared by subsequent instances; source byte strings are shared
between snapshots. Old revisions stay retained while old instances can require
them. Budget that retention separately from Lua's heap cap.

Domain scripts have a catalog-only `require(identity)`:

```lua
local formatting = require("ui.formatting")
local count = model.field("count")
return {
    tick = function()
        local display = formatting.describe(model.get(count))
        model.request("publish_display", display)
    end,
}
```

Imports cache returned values per instance. Dependencies resolve from that
instance's pinned catalog, including modules first imported after a newer
revision was published. Missing modules, cycles, more than 32 nested imports,
invalid source and nil module results fail through the protected boundary.
There is no filesystem/native-DLL fallback, search path, `package` or bytecode
loader. Standalone sessions retain their restricted environment and omit the
domain-only catalog/native capabilities.

## Scoped MVVM listeners

```lua
local count = model.field("count")
local token
return {
    enable = function()
        token = model.subscribe(count, "count_changed")
    end,
    count_changed = function(value)
        model.request("refresh_display", value)
    end,
    disable = function()
        model.unsubscribe(token)
    end,
}
```

`subscribe` accepts a resolved field/action/event slot and a named handler from
the same instance. Tokens are scoped to that instance and are not portable data
or asset handles. Subscription edits are producers: successful handler completion
queues them; `synchronize()` applies them outside every model flush. New listeners
do not observe notifications before that boundary. An unsubscribe requested from
a notification handler takes effect at the next synchronization, so the current
notification traversal remains stable. Do not recursively flush or synchronize
from a Lua/native-service callback.

Subscription admission preserves original native errors. If adding a batch fails,
its newly added prefix is retired and removals have not yet been applied; the
instance fails. Previously committed model state is retained. Disable, removal,
successful reload and shutdown remove native listeners and pending listener edits.
Weak observer endpoints cannot keep an instance or interpreter alive.

## Services, jobs and results

External projects/add-ons register `service_definition` signatures and retained
`synchronous_service` implementations. Domain scripts call
`native.call(name, ...scalars)`. The boundary validates exact parameter/result
counts and types, finite values, text bounds and the per-handler service-call
budget. Original C++ exceptions propagate through Lua. Services execute on the
coordinator and must be inexpensive; instruction quotas cannot preempt arbitrary
native code. Their side effects are outside model-demand rollback, so prefer
validated reads or operations with explicitly defined commit semantics.

Use `model.request(name, ...scalars)` for heavy work. `domain::take_requests()`
returns `instance_work` records with the original instance/generation and the
existing worker-safe cancellation/completion transport. The composition root
dispatches those requests through its shared job system or streamer. Workers
receive no Lua objects and never enter the interpreter. `deliver_results()`
delivers each enabled instance's ready submission-order prefix through the
director's `on_result` handler. Readiness can cross frames; delivery still requires
the currently admitted model/session frame.

Disable cancels outstanding work and retires listeners while preserving Lua
locals. Enable creates a fresh completion mailbox; old completions stay rejected.
Reload constructs fresh locals/modules and admits the candidate's frame before
publishing it. A failed candidate leaves the old generation, subscriptions and
work intact. A successful reload returns a new handle, closes the old mailbox,
removes old listeners and drops its debugger connection. Handles include domain,
registration sequence and reload generation; retired handles never target a
replacement instance.

## Cross-domain messages

Domains exchange owned scalar snapshots through the coordinator, never Lua
tables/functions/stacks. `enqueue(domain_message)` validates the destination
generation, enabled state, exact current frame, a strictly increasing ingress
sequence and bounded payload. The sequence namespace belongs to the receiving
domain; the coordinator orders multiple producers before enqueueing.

`deliver_messages()` enters the receiver through the director in ingress order.
It revalidates the target, discards messages for removed/reloaded/disabled
instances with a diagnostic warning, and consumes attempted messages once even
if their handler fails. Remaining messages require explicit recovery/delivery.
The next domain frame cannot begin while ingress from the current frame remains
undelivered. Workers use completion mailboxes; `enqueue` is an owner-thread API.

## UI composition and lifetime

`atlas::ui_scripting::event_handler` accepts a shared domain and instance handle
as an alternative to a standalone session. Multiple widget/menu adapters can
target instances in the same UI domain. The adapter forwards lifecycle/input
metadata and validates the existing boolean response protocol through the same
director. It does not own instance activation/removal policy.

The composition root delivers `disable`/`end` while the instance is still enabled,
then calls `set_enabled(false)` or `remove`. For reactivation, admit the model's
current frame, enable the instance, deliver `enable`, then synchronize listeners.
Replace adapters holding old generation handles after reload. Keep application
state outside presentation instances; UI ViewModels project or validate access
to that authority rather than duplicating it.

## Diagnostics and measurement

Domain dispatch, listener synchronization, ingress and native services have
profiling scopes. Existing session load/call/result spans and failure/lifecycle
logs remain active where configured. Profiling follows auto/on/off policy.
Callbacks run on the coordinator; sharing an interpreter does not add Lua
multithreading. Debugging stays an explicit development capability and can target
a selected domain instance through `attach_debugger(instance, options)`.

Build/run `atlas_scripting_benchmark` in Release to compare 1, 16 and 64 instances
against equivalent standalone sessions. It warms the same counter workload,
reports nanoseconds per behavior, live requested Lua bytes and warmed Lua PMR
allocation requests. Initialization/reporting are excluded from timing. The
smoke test verifies workload completion, without elapsed-time thresholds.
The reported bytes exclude C++ host metadata, model storage, source catalogs,
services, allocator fragmentation and process working set; they are not total
application footprint or a claim about all UI workloads. Retired Lua objects may
remain until garbage collection progresses; shutdown closes the shared heap.
