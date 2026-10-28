# MVVM and events

## State ownership

The ViewModel is the source of truth for presentation state. Widgets keep derived visual state and caches, not competing authoritative copies of XP, health, or other displayed data.

| Role | Responsibility |
| --- | --- |
| Model | Domain rules/services where needed, independent of widgets |
| ViewModel | Presentation state, properties, actions, notifications and derived values |
| View | Widget hierarchy, bindings and interaction forwarding |
| Events | Decoupled action/result communication |

Inventory, quests, stores, leaderboards and progression are sample data domains, not gameplay systems to implement now. If gameplay is added later, explicitly define its authority and projection into ViewModels.

## Authored XML and prepared wiring

UI and ViewModels are authored as XML. Bindings use names/paths in source documents. Validation and cooking resolve types and wiring, then generate construction functions. Do not require repeated string lookup for every frame when handles/accessors can be resolved beforehand.

```text
ViewModel XML: properties, named bindings, actions
    -> validate names/types/dependencies
    -> generated construction function + prepared data
    -> compile/link
    -> instantiated state and notifications
```

Each UI document should yield a construction function; ViewModels follow the same direction. Generated C++ must be compiled/linked to become executable code. A binary asset file does not automatically become a C++ function.

Keep useful names in authoring diagnostics. Define stable IDs/handles and detect missing references, type mismatches and collisions before generation.

## Example flow

```text
click Gain Experience
    -> ViewModel action/command
    -> event/action updates authoritative presentation state
    -> notification and derived-property updates
    -> binding updates progress bar and text
    -> affected regions become dirty
```

Apply Damage uses the same decoupled path. The button does not know which progress bar changes. Event-driven design does not require a global event bus.

## Concurrency

Process independent work in jobs and publish observable changes at defined frame boundaries. Declare which threads can produce changes and how they are merged. Do not perform unsynchronized writes to widget/component state from arbitrary callbacks.

## Open contracts and acceptance

One-way/two-way/one-time binding, converters, validation, command availability, queueing versus immediate delivery, reentrancy, failure handling, subscription ownership, and collection-change notifications remain open. Define precedence between binding, animation and manual editing.

Tests must cover propagation, removal/unsubscription, ordered-event reproducibility, wiring errors, incremental collections, and equivalence between interpreted preview and generated construction.
