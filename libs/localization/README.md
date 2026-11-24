# Localization

Reusable XML string tables, per-context language selection and cardinal plurals.
The portable public API separates owner publication from native plural data.

See [the contract](../../docs/localization.md) and the public headers under
`include/atlas/localization`. Acquire bytes through the streamer; this library
never reads application files directly.
