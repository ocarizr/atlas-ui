/** @file @brief Authorized typed roots, atomic catalog replacement and source-copy ownership. */
#include "asset_files.hpp"
#include "asset_acquisition.hpp"
#include "document_file.hpp"
#include <random>

namespace atlas::editor
{
namespace
{
/// @brief Name both sides of a path authorization request.
struct path_boundary
{
    const std::filesystem::path& root;      ///< Canonical trusted directory.
    const std::filesystem::path& candidate; ///< Path to authorize.
};

/// @brief Canonical containment applies to metadata as well as source paths.
std::filesystem::path contained(path_boundary boundary)
{
    const auto actual = std::filesystem::weakly_canonical(boundary.candidate);
    const auto relative = actual.lexically_relative(boundary.root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    {
        throw std::invalid_argument("Editor asset path escapes its authorized root");
    }

    return actual;
}

/// @brief Provision the namespace once; reopening always consumes its persisted identity.
assets::catalog empty_catalog()
{
    std::random_device entropy;
    assets::namespace_id space;
    for (auto& value : space.bytes)
    {
        value = static_cast<std::uint8_t>(entropy());
    }

    space.bytes.front() |= 1;
    return assets::catalog::compile({space});
}

/// @brief Copy-on-write sources keep earlier catalog snapshots recoverable.
class native_asset_store final : public asset_store
{
  public:
    native_asset_store(std::filesystem::path root, jobs::job_system& jobs,
                       diagnostics::services sinks)
        : root_{std::move(root)}, jobs_{jobs}, sinks_{std::move(sinks)}
    {
    } ///< Composition services outlive accepted reads.

    std::unique_ptr<asset_read> read(const std::filesystem::path& source,
                                     assets::asset_type type) override
    {
        return acquire_editor_asset(source, type, jobs_, sinks_);
    } ///< Explicit external selection grants source acquisition only.

    std::filesystem::path locate(const assets::source_locator& source) const override
    {
        (void)assets::parse_type(source.root);
        const auto typed_root = contained({root_, root_ / source.root});
        return contained({typed_root, typed_root / std::filesystem::path{source.path}});
    } ///< Directory links are resolved before source access.

    bool exists(const assets::source_locator& source) const override
    {
        return std::filesystem::is_regular_file(locate(source));
    } ///< No content acquisition is performed for list rows.

    void create(const assets::source_locator& target, std::span<const std::uint8_t> bytes) override
    {
        const auto path = locate(target);
        std::filesystem::create_directories(path.parent_path());
        write_editor_source(locate(target),
                            {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, false);
    } ///< A transaction owns only a newly created source.

    void rollback(const assets::source_locator& target) override
    {
        remove_document(locate(target));
    } ///< Called only after a successful private create.

    void publish(const assets::catalog& snapshot) override
    {
        const auto bytes = assets::write_xml(snapshot, sinks_);
        if (bytes.size() > std::size_t{4} * 1024 * 1024)
        {
            throw std::length_error("Editor catalog exceeds its 4 MiB metadata budget");
        }

        write_editor_source(contained({root_, root_ / "catalog.xml"}), bytes);
    } ///< Single-file atomic publication is the durable transaction commit.
  private:
    std::filesystem::path root_;  ///< Canonical origin independent of the current directory.
    jobs::job_system& jobs_;      ///< Application's one worker pool.
    diagnostics::services sinks_; ///< Logs and profiling are injected.
};

/// @brief The same fixed type vocabulary owns directories for editor authoring and preview.
std::vector<streaming::source_root> source_roots(const std::filesystem::path& root)
{
    std::vector<streaming::source_root> output;
    for (unsigned value = 0; value <= static_cast<unsigned>(assets::asset_type::widget); ++value)
    {
        const auto type = static_cast<assets::asset_type>(value);
        const auto name = std::string{assets::format_type(type)};
        output.push_back({name, root / name});
    }

    return output;
}
} // namespace

/// @brief Read metadata through streaming and publish a fresh namespace only for a new origin.
std::unique_ptr<asset_repository> open_asset_repository(const std::filesystem::path& directory,
                                                        jobs::job_system& jobs,
                                                        const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"editor.assets.open", "editor.assets"}};
    std::filesystem::create_directories(directory);
    const auto root = std::filesystem::canonical(directory);
    for (const auto& type : source_roots(root))
    {
        std::filesystem::create_directories(type.directory);
        (void)contained({root, type.directory});
    }

    auto storage = std::make_shared<native_asset_store>(root, jobs, sinks);
    const auto metadata = contained({root, root / "catalog.xml"});
    const bool exists = document_exists(metadata);
    auto initial = exists ? assets::read_xml({read_editor_source(metadata, jobs, sinks)}, sinks)
                          : empty_catalog();
    if (!exists)
    {
        storage->publish(initial);
    }

    return std::make_unique<asset_repository>(std::move(initial), std::move(storage), sinks);
}

/// @brief Every view retains its immutable mount independently of later catalog edits.
std::shared_ptr<streaming::catalog_streamer> mount_editor_assets(const assets::catalog& catalog,
                                                                 const std::filesystem::path& root,
                                                                 jobs::job_system& jobs,
                                                                 const diagnostics::services& sinks)
{
    const auto roots = source_roots(std::filesystem::canonical(root));
    return std::make_shared<streaming::catalog_streamer>(
        jobs, catalog, streaming::prepare_loose_mount(catalog, roots, sinks),
        streaming::streamer_limits{}, nullptr, sinks);
}
} // namespace atlas::editor
