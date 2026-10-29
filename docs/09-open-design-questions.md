# Open design questions

## Established decisions

Windows only; C++20; Direct3D 12 only; EnTT; XML serialized content and ViewModels; YAML configuration; gRPC/Protobuf IPC; reusable libraries and separate editor/game apps; persistent workers; streaming across frames; same-frame visual phases; deterministic presentation; C++ extensions with rebuild/restart; DDD and TDD.

These choices are closed unless an explicit design change reopens them.

## Next implementation contracts

1. **First UI:** what reference screen or minimum image/text/button composition should be shown? Which sizing/input behavior is needed immediately?
2. **Dependencies:** vcpkg and a pinned baseline are established. Which upgrade policy and media encoding toolchain should follow?
3. **Preview recovery:** restore only authoring/selection/view state, or also transient animation, XP/health and simulation state after rebuilding?
4. **Frame definition:** does completion mean CPU phases/submission, GPU completion, or presentation? How do late events and cycles behave?
5. **Determinism:** what replay/time/publication contract is required, and is bitwise equality across hardware necessary?

## Runtime and content

- Scheduler queue policy, worker count, affinity, parent/child completion, cancellation, allocation and shutdown.
- EnTT component access ownership, structural changes and stable references across phases.
- Layout units/DPI, anchors, clipping/scroll, focus/navigation, drag/drop, virtualization and accessibility.
- XML schemas for UI, ViewModels, animation/localization and metadata; names/IDs, migrations and generated-code compatibility.
- Binding directions, converters, command validation, event ordering, subscriptions, collections and competing property writers.
- Reflection eliminated by cooking versus metadata retained for authoring and runtime extensions.
- RAM/VRAM budgets, priorities, caching/eviction, fallbacks, failed loads, cancellation and hot reload.
- Animation clocks, interpolation/blending, scrub/seek/loop events and UI sound-command timing.
- AV1 encoding profiles, measured decoder budgets, UI-SFX voice/device policy; text shaping, Unicode, fallback fonts, plurals and RTL requirements.

## Editor and delivery

- Protobuf service boundaries, protocol evolution, session/revision IDs, reconnect and state restoration.
- Preview image transport and input/selection coordinate mapping across resize/DPI changes.
- Native extension registration/linkage, supported hooks, ABI/API versioning and editor self-restart.
- Undo/redo, autosave/recovery, multiple documents, device simulation and Package Manager scope.
- Reference hardware and numerical frame/memory/load/speedup budgets.
- Coverage thresholds, visual comparison tolerances, CI GPU availability and profiler export needs.

Track accepted answers in the subsystem document. Do not treat an unresolved implementation detail as an approved contract.
