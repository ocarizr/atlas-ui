# Localization

`atlas::localization` owns reusable string tables and independent language contexts.
It has no UI, editor, filesystem, scheduler or native SDK types in its public API.
Applications obtain XML bytes through the streamer and call `parse_xml`; native
factories can construct the same `definition` and use identical validation.

## Tables and lookup

```xml
<localization fallback="en">
  <language tag="en" direction="ltr">
    <string key="settings.title">Settings</string>
    <plural key="inventory.items">
      <form category="one">One item</form>
      <form category="other">Items</form>
    </plural>
  </language>
  <language tag="ar" direction="rtl">
    <string key="settings.title">إعدادات</string>
  </language>
</localization>
```

The fallback language defines the complete key schema. Other languages may omit
keys, but cannot introduce unknown ones. Each message contains either one literal
(including empty/whitespace-only text) or cardinal forms with a required `other`.
Forms are `zero`, `one`, `two`, `few`, `many`, `other`. Unsupported/duplicate tags,
keys, forms, nested literal markup, invalid UTF-8 and malformed XML are rejected.
Text remains UTF-8; no Unicode normalization or case folding is performed on keys.

Language tags use a bounded ASCII BCP-47-shaped syntax, normalized to lowercase:
an initial 2–8-letter language followed by hyphen-separated 1–8-character
alphanumeric subtags, at most 63 bytes. This does not implement every grandfathered
BCP-47 spelling or validate language registry membership. No system locale is read
implicitly. Lookup tries the requested tag, progressively shorter parent tags,
then the explicit fallback, independently for each key. Unknown requested languages
therefore use the configured fallback. Resolved text reports the actual translation
locale and direction, rather than incorrectly inheriting those of the request.

`context::key` resolves a name once. A compact context-owned key indexes a
pre-resolved translation map in constant time after selection. Reload must retain
the exact fallback-key schema; declaration order can change. Schema changes require
rebuilding bindings in a new context. Foreign handles and missing keys fail explicitly.

Plural selection requires a finite count. Rules belong to the translation locale;
a missing category uses that message's `other` form. Messages are selected as authored:
count insertion, ICU MessageFormat, arbitrary parameter substitution, decimal display
formatting, dates, collation and ordinal selection are outside this API.

## Publication and lifetime

Create independent contexts for editor chrome, document previews and runtime views.
Select or reload only on the creating thread, at the application's owner phase
boundary. Tables, indices and notification batches are prepared before publication.
Validation/allocation failure or a cancelled reload leaves the old revision intact.
Selecting the same canonical requested tag is a no-op; a successful reload publishes
a new revision. Retained `resolved_text` owns its strings and survives later changes.

Observers run in registration order after commit. They may resolve/read text and
cancel tokens; selecting, reloading or registering recursively is rejected. Callback
exceptions are logged and later observers still run: they do not undo an already
published language. Consumers retain their old output on refresh failure and can retry.
Move-only subscriptions disconnect on destruction; cancelling a captured later token
suppresses its delivery. Context teardown disables all callbacks without invoking them.

Admission: at most 128 languages, 65,536 messages per language, 128 UTF-8 bytes per
key, 16,384 bytes per literal/form, 8 MiB XML/decoded key-and-text bytes, and 1,024
live observers per context. Indexed fallback storage is bounded by the language/key
product. These limits bound individual operations, not total process memory. Native
string/vector/control-block allocations currently use the standard allocator; this
library does not override global allocation. The memory audit remains tracked in #116.

## Platform and diagnostics

Windows Desktop uses the SDK ICU C API (`icu.lib`, Windows 10 1903 or newer) for
cardinal rules. The implementation lives under `libs/platform/windows/localization`
and is selected by `ATLAS_TARGET_PLATFORM` and native integration settings. No new
vcpkg dependency or handwritten English-only language-rule fallback is introduced.
The SDK-free profile supports tables, contexts and injected selectors; default native
plural selection reports unavailable. Literal-only contexts still work in that profile.

ICU/CLDR data and installed fonts are OS-dependent. Results are deterministic for
the same inputs and backend data, not a promise of identical language data across
Windows releases. The current adapter opens/closes a native rules object per plural
resolution; selection is change-driven, not a required per-frame operation. Cache or
batch only after measuring representative repeated-count workloads.

Injected diagnostics cover `localization.parse`, `localization.construct`, `localization.resolve`,
`localization.publish` and `localization.reload`. Failures and observer exceptions
log when a destination is configured. Profiling follows the global AUTO/ON/OFF policy.
No worker pool or per-label job is created. A context publishes on the owner; independent
owned resolved text can be passed to later jobs that do not mutate its context.

## UI integration

`ui_text::localized_label` subscribes to a pre-resolved key, owns its font family and
retains immutable text/provenance/glyph snapshots. It refreshes only after language,
reload or explicit count changes, shaping with the translation locale and direction.
`set_count` and `refresh` require an explicitly live context. Failed shaping preserves
the previous snapshot; the context may already have a newer committed revision.

At the owner boundary call `apply_metrics(tree, element)` before layout. It publishes
natural text dimensions through `element_tree::set_layout`, retaining authored
constraints, margins, identity and hierarchy. Unchanged metrics cause no extra layout
invalidation. Width-dependent wrapping still requires a refresh with an appropriately
configured label width; this API does not automatically mirror the widget tree or
rebuild/upload glyph atlases. Use the existing text paint/upload lifecycle for the
new glyph snapshot. No declarative localization inspector or automatic application-wide
language settings UI is supplied by the library.

## Measurement

`atlas_localization_benchmark` measures warmed owner language swaps plus resolution
of every pre-bound key for 16/1,024 keys, four languages and 32 observers. It verifies
fallback provenance and performs a separate untimed C++ allocation-request census.
One thousand unchanged selections must neither publish nor allocate. `--smoke`
checks the same contract with fewer samples and no timing threshold. It excludes
startup, XML loading, plural-rule selection, native shaping, GPU work and storage.
Cumulative requested C++ bytes are not live/peak memory, RSS or fragmentation evidence.
