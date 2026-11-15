/** @file @brief Resolve material assets exclusively through the typed storage boundary. */
#include "material_sources.hpp"
#include "document_validation.hpp"
#include "material_validation.hpp"
#include "storage.hpp"
#include <atomic>

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Match the generic shader source ABI limit.
constexpr std::size_t max_shader_bytes = std::size_t{1024} * 1024;
constexpr std::size_t max_retained_shader_bytes = 16 * max_shader_bytes; ///< Candidate budget.

/// @brief A replacement candidate cannot reuse a key still present in an earlier frame.
std::uint64_t next_shader_key()
{
    static std::atomic<std::uint64_t> next{1};
    auto key = next.load(std::memory_order_relaxed);
    while (key != std::numeric_limits<std::uint64_t>::max())
    {
        if (next.compare_exchange_weak(key, key + 1, std::memory_order_relaxed))
        {
            return key;
        }
    }

    throw std::overflow_error("UI material shader keys exhausted");
}
} // namespace

/// @brief Prepared construction has the same bounded, typed inputs as the XML path.
material_sources::material_sources(const construction& options) : options_{options}
{
    if (options.materials.size() > max_elements)
    {
        throw std::invalid_argument("Too many prepared UI materials");
    }

    for (const auto& entry : options.materials)
    {
        if (entry.source.type != assets::asset_type::material || !entry.revision || !entry.value ||
            !prepared_.emplace(entry.source, entry).second)
        {
            throw std::invalid_argument("Invalid or duplicate prepared UI material");
        }
    }
}

/// @brief Record each dependency once and refuse closed or unrelated mounts.
void material_sources::depend_on(assets::asset_ref source)
{
    if (!dependencies.contains(source))
    {
        const auto stamp = options_.storage.stamp(source);
        if (!options_.storage.accepts(stamp))
        {
            throw std::runtime_error("UI material dependency has a stale mount");
        }

        dependencies.emplace(source, stamp);
    }
}

/// @brief Match the mounted revision before accepting a prepared XML replacement.
const material_document& material_sources::load(assets::asset_ref source)
{
    if (const auto loaded = descriptors_.find(source); loaded != descriptors_.end())
    {
        return loaded->second;
    }

    depend_on(source);
    const auto info = options_.storage.describe(source);
    if (const auto entry = prepared_.find(source); entry != prepared_.end())
    {
        if (entry->second.revision != info.revision)
        {
            throw std::invalid_argument("Stale prepared UI material revision");
        }

        validate_material_document(*entry->second.value);
        return descriptors_.emplace(source, *entry->second.value).first->second;
    }

    const auto bytes = acquire(info, max_material_bytes, options_);
    auto value = parse_material_xml({bytes, assets::format_id(source.id)}, options_.diagnostics);
    return descriptors_.emplace(source, std::move(value)).first->second;
}

/// @brief Collect bounded source bytes for later native reflection on the render coordinator.
std::uint64_t material_sources::shader(assets::asset_ref source, ui::material_kind kind)
{
    const auto pair = std::pair{source, kind};
    if (const auto found = shader_keys_.find(pair); found != shader_keys_.end())
    {
        return found->second;
    }

    depend_on(source);
    const auto info = options_.storage.describe(source);
    const auto maximum = std::min(max_shader_bytes, max_retained_shader_bytes - shader_bytes_);
    auto hlsl = acquire(info, maximum, options_);
    if (hlsl.starts_with("\xef\xbb\xbf"))
    {
        hlsl.erase(0, 3);
    }

    mvvm::validate_text(hlsl, max_shader_bytes);
    if (hlsl.empty())
    {
        throw std::invalid_argument("Empty UI material shader");
    }

    shader_bytes_ += hlsl.size();
    const auto key = next_shader_key();
    shaders.push_back({key, kind, std::move(hlsl)});
    shader_keys_.emplace(pair, key);
    return key;
}

/// @brief Inherit from a single template before publishing an independent per-element copy.
const resolved_material& material_sources::resolve(assets::asset_ref source)
{
    const diagnostics::operation_scope scope{options_.diagnostics,
                                             {"ui.material.resolve", "ui.content"}};
    if (const auto found = instances_.find(source); found != instances_.end())
    {
        return found->second;
    }

    const auto& authored = load(source);
    const auto* replacement = std::get_if<material_instance>(&authored);
    const auto& inherited = replacement ? load(replacement->source) : authored;
    const auto* base = std::get_if<material_template>(&inherited);
    if (!base)
    {
        throw std::invalid_argument("A UI material override must target a material template");
    }

    resolved_material result{base->defaults, base->texture};
    if (base->texture)
    {
        depend_on(*base->texture);
    }

    if (replacement)
    {
        result.style = ui::resolve_material(base->defaults, replacement->parameters);
        if (replacement->texture)
        {
            if (base->defaults.kind != ui::material_kind::texture)
            {
                throw std::invalid_argument("Shape material cannot override a texture");
            }

            depend_on(*replacement->texture);
            result.texture = replacement->texture;
        }
    }

    if (base->shader)
    {
        result.style.shader = shader(*base->shader, base->defaults.kind);
    }

    return instances_.emplace(source, result).first->second;
}
} // namespace atlas::ui_content::detail
