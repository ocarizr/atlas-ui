# Actions and navigation conditions

`atlas::scripting::domain` owns a reusable registry of native and Lua actions and
conditions. Registration is independent of UI composition. A descriptor contains
a stable namespaced ID, display label, category, kind and scalar parameter schema.
Engine, project and add-on origins own their declared prefix. Duplicate live IDs
are rejected; display labels and categories do not determine identity.

Parameters use exact boolean, signed integer, double or string values. Trailing
parameters may have defaults. Integer parameters may declare labeled choices.
Numeric and string coercion is rejected before entering an implementation. IDs
are resolved once with `bind_callable(id, model_context)`; a binding includes the
domain, registration generation and model context. It cannot be serialized or
transferred to another domain.

## Lua modules

Register source bytes through the domain's catalog, then call
`load_callable_module({entry_source, origin})`. Imports use catalog source IDs;
Lua has no filesystem access. The streamer supplies project bytes before this
operation. The entrypoint imports category modules with the catalog-backed
`require`. No source scanning or function-signature inference occurs.

```lua
actions.register({
    id = "project.select_tab",
    label = "Select tab",
    category = "Navigation",
    parameters = {{name = "tab", type = "integer", default = 0}},
    execute = function(context, tab)
        context.set("selected_tab", tab)
    end,
})

navigation_conditions.register({
    id = "project.can_open",
    label = "Can open",
    category = "Navigation",
    evaluate = function(context)
        return context.get("enabled")
    end,
})
return {}
```

The host injects the context as the first argument. Scripts call its functions
with dot syntax. The parameter schema excludes that context. Registry modules
have no `model` or synchronous `native.call` global. Registration is allowed only
during initialization. Modules should return a table, as ordinary catalog modules
do. Metadata discovery does not invoke registered actions or predicates.

Native callbacks use the same descriptors and typed arguments. Actions receive
`action_context&`; predicates receive `const condition_context&`. Both read
committed values with `get`. Actions can additionally stage `set`, `invoke`,
`emit` and `request` demands. These are admitted only after successful return;
the ViewModel processes them at its explicit synchronization phase. Staged writes
are not read back during a callback. Actions return no values; conditions return
exactly one boolean.

## Ownership, failures and budgets

Registry operations run on the domain owner, outside an existing dispatch.
Invocation requires a healthy model with the same frame as the domain. Native
registrations allocate no Lua interpreter. Lua registry contexts share the
domain's one physical interpreter with ordinary behaviors, while keeping module
globals isolated between bound models.

Lua discovery and execution use the existing source, memory and instruction
limits. Calls share a per-registry frame budget. Demands and outstanding work use
the session command, text, pending-work and pending-byte limits. Registry capacity
is 512 distinct IDs, including retired slots, and 128 live bound contexts.
Descriptors allow 32 parameters and 64 choices per parameter. Default payloads
are bounded to 64 KiB per descriptor. Copied descriptor metadata is bounded to
8 MiB per module and per registry.

A `condition_node` is a bound leaf or a nonempty ordered AND group, with optional
negation on either. The entire expression is checked before execution, including
branches that short circuit. Expressions allow 64 nodes and depth eight. Unknown
IDs, stale bindings, invalid arguments and execution failures deny navigation;
negation never turns errors into permission. Conditions cannot stage commands,
events or native work through their context.

Conditions must remain pure: the restricted context prevents host side effects,
but cannot prove that Lua code does not modify its own private upvalues. Native
callbacks are trusted cooperative code; captured external state and arbitrary
C++ execution are outside the capability boundary and cannot be preempted by the
Lua instruction limiter.

Reload validates the complete replacement before publishing it. Failed discovery
preserves existing bindings. Successful replacement invalidates old bindings,
cancels outstanding work and retires the old module contexts. Lua invocation
errors quarantine that module/model context until replacement. Removing one Lua
entry also retires its shared module contexts; other active entries can be rebound.

## Native work and Flow

Actions enqueue cheap value-only descriptions with `request`. The owner drains
`take_callable_requests()`, chooses the matching native implementation and
dispatches ready CPU work through the shared job dispatcher. Workers receive
owned scalar values and `work_request`, never a Lua VM or mutable model. Use the
streamer for storage and dispatch decoding only when its input is ready.

Workers call `work_request::complete(values, success)`. The owner drains
`take_callable_results()` in context/request order and explicitly routes accepted
results during its next permitted phase. An unfinished earlier request holds
later results in that context. Retired destinations reject late completion.
The registry itself creates no worker pool and waits for no work.

`ui_content::make_flow_guard_evaluator` adapts transition IDs and bound condition
expressions to `flow_manager`'s optional outgoing guard service. The domain
outlives the manager, and both use the same owner. Guards run after checking the
source presentation, during pending loads and immediately before publication.
False rejects a candidate; invalid or failed evaluation records a failed Flow
transaction. The previous presentation remains available.

## Contributor generation

`generate_callable_category` emits a complete Lua registration module from typed
descriptors, including LuaLS annotations and safe scalar defaults. Generated
actions have empty bodies; generated conditions return false until implemented.
`generate_callable_entrypoint` emits deterministic catalog imports. Both return
source bytes without reading or writing files. The editor's project write service
owns persistence and asset registration. These APIs provide persistence-independent
building blocks for editor category creation.

The declarations under `tools/lua_api/` support LuaLS completion for these APIs.
Runtime bindings are distinct from authored XML IDs. Persist stable authored IDs
and resolve them when constructing a live domain.
