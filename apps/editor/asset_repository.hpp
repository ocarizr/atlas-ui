/** @file @brief Editor-owned catalog transactions over a separate persistence boundary. */
#pragma once
#include <atlas/streaming/catalog_streamer.hpp>
#include <functional>

namespace atlas::editor
{
/// @brief Worker-prepared input for a private owner transaction.
struct asset_payload
{
    std::shared_ptr<const streaming::asset_bytes> bytes; ///< Bounded acquired bytes.
    std::vector<assets::asset_ref> dependencies;         ///< Validated typed references.
    std::string digest;                                  ///< SHA-256 of acquired source bytes.
};

/// @brief An acquisition owns all callback state and drains before destruction.
class asset_read
{
  public:
    virtual ~asset_read() = default;    ///< Retire accepted work on its owner.
    virtual bool ready() const = 0;     ///< Poll without waiting.
    virtual asset_payload take() = 0;   ///< Consume only after readiness.
    virtual void cancel() noexcept = 0; ///< Request cooperative cancellation.
};

/// @brief Infrastructure has exclusive file access; catalog mutations remain owner-only.
class asset_store
{
  public:
    virtual ~asset_store() = default; ///< Store outlives accepted reads.
    virtual std::unique_ptr<asset_read> read(const std::filesystem::path& source,
                                             assets::asset_type type) = 0; ///< Stream ready jobs.
    /// @brief Reject unauthorized roots, traversal and links that escape the workspace.
    virtual std::filesystem::path locate(const assets::source_locator& source) const = 0;
    virtual bool exists(const assets::source_locator& source) const = 0; ///< Probe metadata.
    virtual void
    create(const assets::source_locator& target,
           std::span<const std::uint8_t> bytes) = 0; ///< Atomic create without replace.
    virtual void
    rollback(const assets::source_locator& target) = 0;        ///< Remove an owned new source.
    virtual void publish(const assets::catalog& snapshot) = 0; ///< Atomically replace catalog XML.
};

/// @brief Import, register, relink and move use the same revision-checked acquisition.
struct asset_transfer
{
    std::filesystem::path source;  ///< Explicitly selected source, or an authorized locator.
    assets::source_locator target; ///< Destination root must match the asset type.
    assets::asset_type type;       ///< Expected content interpretation.
    std::optional<assets::asset_ref> asset{}; ///< Existing identity for relink/move/refresh.
    bool copy = true;                         ///< False registers bytes already at the target.
};

/// @brief Per-record rows remain bounded by the catalog size.
struct asset_row
{
    assets::asset_record record; ///< Stable identity and importer metadata.
    bool missing = false;        ///< Source currently unavailable.
};

/// @brief Immutable snapshots are published only after persistence succeeds.
class asset_repository
{
  public:
    asset_repository(assets::catalog initial, std::shared_ptr<asset_store> storage,
                     diagnostics::services sinks = {}); ///< Retain one writable origin.
    ~asset_repository(); ///< Cancel and drain acquisition before releasing the store.
    asset_repository(const asset_repository&) = delete;             ///< One authoring owner.
    asset_repository& operator=(const asset_repository&) = delete;  ///< Preserve owner identity.
    [[nodiscard]] const assets::catalog& snapshot() const noexcept; ///< Current saved metadata.
    [[nodiscard]] std::vector<asset_row>
    browse(assets::asset_type type, std::string_view query = {}) const; ///< Filter paths.
    /// @brief Return direct required and derived references.
    [[nodiscard]] std::vector<assets::asset_ref> dependents(assets::asset_ref asset) const;
    void begin(asset_transfer request); ///< Admit at most one bounded preparation.
    bool poll();   ///< Publish a current successful result, or propagate failure without mutation.
    void cancel(); ///< Invalidate the pending generation before requesting cancellation.
    [[nodiscard]] bool busy() const noexcept; ///< Ready acquisition still needs owner polling.
    void remove(assets::asset_ref asset);     ///< Reject referenced assets before persistence.
    /// @brief Advance importer settings and invalidate derived products and dependent revisions.
    void configure(assets::asset_ref asset, std::uint32_t settings_version);
    /// @brief Persist a previous snapshot, rejecting removal of externally referenced identities.
    bool undo(const std::function<bool(assets::asset_ref)>& referenced = {});
    /// @brief Restore the next snapshot with the same reference protection.
    bool redo(const std::function<bool(assets::asset_ref)>& referenced = {});

  private:
    /// @brief Retain one independently owned acquisition until owner collection.
    struct pending
    {
        asset_transfer request;           ///< Private source and destination.
        std::unique_ptr<asset_read> read; ///< All worker output is owned here.
        std::uint64_t generation;         ///< Admission catalog generation.
    };

    void
    commit(assets::catalog_document draft); ///< Validate and persist before publishing history.
    /// @brief Advance transitive dependents and discard stale derived bindings.
    void invalidate(assets::catalog_document& draft, assets::asset_ref changed);
    /// @brief Persist history restoration before moving either stack.
    bool restore(bool forward, const std::function<bool(assets::asset_ref)>& referenced);
    /// @brief Require a live identity of the expected type.
    assets::asset_record& record(assets::catalog_document& draft, assets::asset_ref ref);
    assets::catalog current_;                  ///< Published read-only metadata.
    std::shared_ptr<asset_store> storage_;     ///< Infrastructure retained through pending reads.
    diagnostics::services sinks_;              ///< Optional logging and CPU profiling.
    std::optional<pending> pending_;           ///< Bounded single in-flight preparation.
    std::vector<assets::catalog> undo_, redo_; ///< At most 16 immutable snapshots per direction.
    std::uint64_t watermark_;                  ///< Never reuse an ID exposed before undo.
    std::uint64_t generation_ = 1;             ///< Changed on mutation or cancellation.
};
} // namespace atlas::editor
