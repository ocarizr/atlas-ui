# Semantic palettes and editor themes

`atlas::ui` supplies immutable semantic palettes and explicit owner-thread palette
contexts. `atlas::ui_content` supplies XML, prepared binary and typed streamer
adapters. These libraries do not depend on editor code or a graphics backend.

A palette contains a contract identifier and a complete set of case-sensitive
keys. Identifiers contain ASCII letters, digits, `_`, `-` or `.`, with at most 64
characters. A definition contains 1–256 unique colors. Compatible themes have the
same contract identifier and exactly the same keys, independent of declaration
order. Partial themes, aliases and inheritance are not supported. Missing keys,
duplicate keys, nonfinite channels and incompatible replacements fail validation.

## Authoring and construction

Palette content uses XML:

```xml
<palette version="1" contract="menu.v1">
  <color key="background" value="#17191DFF"/>
  <color key="text" value="#EFE8DCFF"/>
  <color key="accent" value="#BD8A45FF"/>
</palette>
```

Source colors require `#RRGGBBAA`. RGB is sRGB; alpha is linear, straight alpha.
Parsing converts RGB once into linear floats. Native `palette_definition` values
are already linear. `encode_palette`/`decode_palette` preserve exact native floats
under a versioned, extent-checked and checksummed envelope. XML serialization
rounds native colors to the nearest sRGB8 representation; it is not a lossless
storage format for arbitrary native floating-point values. ICC/display color
management is outside this contract.

Use `palette::compile` for native/prepared construction, `parse_palette_xml` for
source bytes, or `load_palette` for a typed `asset_type::palette` reference. The
streamer is the only read boundary. Source and prepared palette payloads are
limited to 64 KiB. Bootstrap loading can wait on the owner; frame-time acquisition
must use staged completed reads, not block CPU workers on I/O. `publish_palette`
checks the captured catalog revision/mount generation before selecting a candidate.
Failed reads, cancellation, invalid definitions and stale publication do not
replace the previously selected palette.

An application owns one `palette_context` per independently styled domain and
passes it through `ui_content::construction::palette`. UI documents retain
semantic sources alongside their literal values:

```xml
<paint fill="true" r="1" g="1" b="1" a="1" color-key="background"/>
```

`color-key` selects the primitive tint. `material-color-key` independently selects
the material tint and requires an inline or referenced material. An empty key
uses the literal value; an unknown key is an error, not a silent fallback. The
host selects the palette asset/context; UI documents do not embed a global theme
or a filesystem path. Prepared UI v11 retains these sources and resolves keys
once when constructing an instance. Earlier prepared versions remain readable;
documents without semantic sources retain their existing encoding.

## Frame ownership and invalidation

Select a compatible immutable snapshot on the context owner, between frame
phases. Call `instance::refresh_palette` before presentation, or let the normal
`synchronize` phase apply it. Capture snapshots on the owner before passing
immutable colors to workers. Mutable context operations from another thread are
rejected.

Instances retain sparse generation-checked bindings to compact palette slots.
An unchanged selected snapshot performs no element traversal. On replacement,
only referenced colors that differ update their paint/material values. Layout
inputs, computed geometry, focus, model state and animation clocks are preserved.
Removed element identities are skipped. No job is scheduled per key or widget;
cheap color publication remains serial.

An explicit semantic source replaces its stored literal base. Paint and material
tints are separate multipliers. Active material animation/state channels retain
their current output; a theme switch updates their underlying base without
overwriting the active sample. Unanimated tint channels update immediately.
The current MVVM scalar binding vocabulary does not expose color channels, so
there is no competing MVVM color writer. Hosts adding native/Lua writers must
respect the declared source and owner phase rather than concurrently mutating
the same values.

Palettes are bounded individually, not a whole-process memory quota. Consumers
can retain older immutable snapshots; those leases count against their own
residency budgets. Palette tables and sparse bindings allocate during compilation
and construction. Timing and allocator evidence must distinguish those costs
from warmed publication and from editor packet copies.

## Editor preferences

The editor exposes a **Theme...** inspector page with light/dark presets, a
semantic swatch selector, a bounded hex draft, Apply, Reset swatch and Reset all
overrides. Preset selection resets overrides. Draft text is private until Apply;
Escape or focus loss cancels editing. Presets are immutable native definitions
compiled by the shared palette technology. This controls colors; the chipped
stone material treatment and broader editor UX redesign are separate work.

The native host reads `%LOCALAPPDATA%/AtlasUI/editor.yaml` through the streamer
and writes through the editor's atomic persistence service. This is user
configuration, outside engine/submodule source and project content:

```yaml
version: 1
preset: dark
overrides:
  accent: '#C19050FF'
```

Unknown/duplicate fields, invalid colors, unsupported presets and unknown keys
are rejected. Startup keeps the default theme if the saved file is invalid.
Edits validate and persist before publishing the new theme; a failed save keeps
the previous selection. No filesystem watcher or implicit configuration hot
reload is installed. SDK-free builds have no native preferences path.

Editor chrome and document preview use separate contexts. Chrome recoloring
retains cached layout, input identities and glyph masks; immutable render packets
are copied to preserve readers. Changing inspector labels or navigating panels
still uses the existing chrome reconstruction path. Literal/semantic paint and
material source controls edit the selected document through one validated undo
transaction. Their choices come from the preview context, not chrome preferences.
The bootstrap host supplies an independent default preview palette; application
hosts may supply a project palette through the reusable construction API.

## Validation and measurements

The palette tests include independent contexts, strict XML/YAML, exact prepared
colors, missing keys, incompatible swaps, animation ownership, stale mounts,
cancellation, failed preference writes and cached editor packet recoloring.
The dense publication case uses 513 bound elements and 20 alternating selections.
It records construction time, publication P50/P95, retained tree PMR bytes and
publication PMR allocation count as GoogleTest XML properties. Timing has no
pass/fail threshold; deterministic assertions check geometry and allocation
invariants.

After building the `ui-content` tests, collect the measurements with:

```powershell
build/windows-msvc/tests/ui_content/Debug/atlas_ui_content_tests.exe `
  --gtest_filter=palette_instance_test.DenseThemePublicationDoesNotAllocateTreePmrOrInvalidateLayout `
  --gtest_output=xml:build/palette-probe.xml
```

Use an optimized build and record hardware/profiling policy before setting a
production budget. This probe measures owner color publication, excluding XML
acquisition, layout, paint packet generation, GPU work and editor packet copies.
Its PMR counters do not include all standard-library, palette-table or process
allocations and are not a whole-process memory bound.
