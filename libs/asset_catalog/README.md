# Asset catalog

`atlas::asset_catalog` owns persistent typed IDs, canonical XML v1 metadata,
and validated read-only snapshots. Catalog parsing consumes caller-owned bytes.
The streamer owns acquisition and the editor owns authoring persistence.

See the [asset and mount contract](../../docs/assets.md).
