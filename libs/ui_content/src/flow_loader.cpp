/** @file @brief Asynchronous menu bootstrap over typed streamer buffers. */
#include "document_validation.hpp"
#include "material_validation.hpp"
#include "prepared_binary.hpp"
#include "prepared_flow_loader.hpp"
#include <atlas/mvvm_content/document.hpp>
#include <atlas/ui_content/flow.hpp>
#include <chrono>
#include <set>

namespace atlas::ui_content
{
namespace
{
constexpr std::size_t max_bootstrap_bytes = std::size_t{16} * 1024 * 1024; ///< Candidate byte cap.
constexpr std::size_t max_bootstrap_assets = 256; ///< Candidate graph reference cap.

/// @brief Only the completion job writes this mailbox; get() synchronizes owner access.
struct delivery
{
    streaming::read_stamp stamp{};   ///< Exact source revision and mount generation.
    streaming::read_result result{}; ///< Move-only lifetime of the delivered data.
};

/// @brief Assign the same source limits as ordinary owner construction.
std::size_t limit(assets::asset_type type)
{
    switch (type)
    {
        case assets::asset_type::ui:
        case assets::asset_type::menu:
        case assets::asset_type::submenu:
        case assets::asset_type::popup:
        case assets::asset_type::widget:
            return detail::max_document_bytes;
        case assets::asset_type::material:
            return detail::max_material_bytes;
        case assets::asset_type::view_model:
            return mvvm_content::content_limits{}.max_bytes;
        case assets::asset_type::script:
            return scripting::session_limits{}.max_source_bytes;
        case assets::asset_type::shader:
            return std::size_t{1024} * 1024;
        default:
            throw std::invalid_argument("Unsupported flow bootstrap asset");
    }
}

/// @brief Gather only composition dependencies; GPU residency remains a host concern.
class stream_load final : public flow_load
{
  public:
    stream_load(const construction& options, flow_load_request request,
                std::shared_ptr<const prepared_flow_screen> contract = {})
        : options_{options}, request_{std::move(request)}, contract_{std::move(contract)},
          propagate_{options.reads.cancellation, [source = stop_]() mutable
                     {
                         source.request_stop();
                     }}
    {
        options_.reads.cancellation = stop_.get_token();
        enqueue(request_.source);
    } ///< Retain no raw pointers to a manager or UI tree in worker callbacks.

    ~stream_load() override
    {
        stop_.request_stop();
    } ///< Destruction is nonblocking; accepted reads own their mailbox independently.

    /// @brief Poll the current read, expand its typed dependencies, or compose a ready view.
    std::unique_ptr<instance> poll() override
    {
        if (stop_.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        if (read_.valid())
        {
            if (read_.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
            {
                return {};
            }

            collect();
        }

        if (cursor_ < pending_.size())
        {
            begin(pending_.at(cursor_++));
            return {};
        }

        options_.staged = std::span<const staged_asset>{ready_};
        options_.navigation = request_.controls;
        options_.navigate = request_.dispatch;
        if (binary_view_)
        {
            return build_prepared(*binary_view_, options_);
        }

        if (!source_)
        {
            throw std::logic_error("Flow screen has no authored document");
        }

        return instance::build(*source_, options_);
    }

  private:
    /// @brief Deduplicate typed assets before issuing reads.
    void enqueue(assets::asset_ref source)
    {
        if (seen_.insert(source).second)
        {
            if (seen_.size() > max_bootstrap_assets)
            {
                throw std::length_error("Flow bootstrap exceeds its dependency budget");
            }

            pending_.push_back(source);
        }
    }

    /// @brief Reserve only a bounded exact read. The worker moves bytes and exits.
    void begin(assets::asset_ref source)
    {
        const auto info = options_.storage.describe(source);
        const auto maximum = contract_ && assets::is_ui(source.type) ? detail::max_prepared_bytes
                                                                     : limit(source.type);
        if (!info.byte_size || info.byte_size > maximum ||
            info.byte_size > max_bootstrap_bytes - admitted_)
        {
            throw std::length_error("Flow bootstrap exceeds its byte budget");
        }

        const auto size = static_cast<std::size_t>(info.byte_size);
        admitted_ += size;
        slot_ = std::make_shared<delivery>();
        read_ = options_.storage.read(
            {source, 0, size, info.revision},
            [out = slot_](streaming::read_stamp stamp, streaming::read_result result)
            {
                out->stamp = stamp;
                out->result = std::move(result);
            },
            options_.reads);
    }

    /// @brief Match an independently stored view against the Flow dependency contract.
    void discover_prepared(const prepared_flow_screen& contract, std::string_view bytes)
    {
        auto view = decode_prepared(std::as_bytes(std::span{bytes}));
        if (view.source != contract.source || view.dependencies != contract.dependencies)
        {
            throw std::invalid_argument("Prepared Flow screen violates its dependency contract");
        }

        detail::validate_prepared_mount(view.dependencies, options_);
        for (const auto& asset : view.dependencies)
        {
            if (asset.source.type == assets::asset_type::script ||
                asset.source.type == assets::asset_type::shader)
            {
                enqueue(asset.source);
            }
        }

        binary_view_ = std::move(view);
    }

    /// @brief Expand only sources that introduce additional composition dependencies.
    void discover(assets::asset_ref source, std::string_view bytes)
    {
        if (assets::is_ui(source.type))
        {
            if (contract_)
            {
                discover_prepared(*contract_, bytes);
                return;
            }

            const auto& authored = source_.emplace(
                parse_xml({bytes, assets::format_id(source.id)}, options_.diagnostics));
            validate_presentation(authored.presentation, source.type);
            for (const auto& model : authored.models)
            {
                const auto native = std::ranges::any_of(options_.factories,
                                                        [&](const auto& entry)
                                                        {
                                                            return entry.source == model.source;
                                                        });
                const auto compiled = std::ranges::any_of(options_.model_schemas,
                                                          [&](const auto& entry)
                                                          {
                                                              return entry.source == model.source;
                                                          });
                if (!native && !compiled)
                {
                    enqueue(model.source);
                }
            }

            for (const auto& element : authored.elements)
            {
                if (element.script)
                {
                    enqueue(element.script->source);
                }

                if (element.material)
                {
                    material(*element.material);
                }
            }
        }
        else if (source.type == assets::asset_type::material)
        {
            material_dependencies(
                parse_material_xml({bytes, assets::format_id(source.id)}, options_.diagnostics));
        }
    }

    /// @brief Follow either a prepared material descriptor or the same XML as normal loading.
    void material(assets::asset_ref source)
    {
        const auto found =
            std::ranges::find(options_.materials, source, &prepared_material::source);
        if (found == options_.materials.end())
        {
            enqueue(source);
        }
        else if (found->value && prepared_.insert(source).second)
        {
            if (prepared_.size() > max_bootstrap_assets)
            {
                throw std::length_error("Flow bootstrap exceeds its prepared material budget");
            }

            material_dependencies(*found->value);
        }
    }

    /// @brief An override points to a typed template and a template may point to a shader.
    void material_dependencies(const material_document& definition)
    {
        if (const auto* base = std::get_if<material_template>(&definition))
        {
            if (base->shader)
            {
                enqueue(*base->shader);
            }
        }
        else
        {
            material(std::get<material_instance>(definition).source);
        }
    }

    /// @brief Consume a completed mailbox and reject cancellation, failure or stale generations.
    void collect()
    {
        read_.get();
        if (!slot_ || !options_.storage.accepts(slot_->stamp))
        {
            throw std::runtime_error("Stale flow asset completion");
        }

        if (const auto* error = std::get_if<std::exception_ptr>(&slot_->result))
        {
            std::rethrow_exception(*error);
        }

        auto data =
            std::get<std::shared_ptr<const streaming::asset_bytes>>(std::move(slot_->result));
        if (!data)
        {
            throw std::runtime_error("Missing flow asset data");
        }

        discover(slot_->stamp.reference,
                 {reinterpret_cast<const char*>(data->data()), data->size()});
        ready_.push_back({slot_->stamp, std::move(data)});
        slot_.reset();
    }

    construction options_;      ///< Owner services, including the typed catalog mount.
    flow_load_request request_; ///< Owned view-specific control bindings.
    std::shared_ptr<const prepared_flow_screen> contract_; ///< Optional cooked dependency contract.
    std::stop_source stop_; ///< Cancellation shared with every admitted read.
    std::stop_callback<std::function<void()>> propagate_; ///< Forward host cancellation.
    std::optional<document> source_;                      ///< Parsed owner-local candidate.
    std::optional<prepared_view>
        binary_view_;                        ///< Decoded prepared UI in the same composition path.
    std::vector<assets::asset_ref> pending_; ///< Deterministic discovery order.
    std::set<assets::asset_ref> seen_;       ///< Unique streamed payloads.
    std::set<assets::asset_ref> prepared_;   ///< Visited prepared descriptors.
    std::vector<staged_asset> ready_;        ///< Retained exact revision buffers.
    std::future<void> read_;                 ///< At most one accepted request per screen candidate.
    std::shared_ptr<delivery> slot_;         ///< Worker-owned until the request completes.
    std::size_t admitted_ = 0;               ///< Total logical bytes for this candidate.
    std::size_t cursor_ = 0;                 ///< Next admitted source.
};
} // namespace

/// @brief Use the regular typed streamer and compose only after all bootstrap bytes are ready.
flow_loader make_flow_loader(const construction& options)
{
    return [options](flow_load_request request)
    {
        return std::make_unique<stream_load>(options, std::move(request));
    };
}

/// @brief Keep each expected dependency record alive for all accepted screen requests.
flow_loader detail::make_prepared_flow_loader(const construction& options,
                                              std::vector<prepared_flow_screen> screens)
{
    auto records = std::make_shared<const std::vector<prepared_flow_screen>>(std::move(screens));
    return [options, records = std::move(records)](flow_load_request request)
    {
        const auto found =
            std::ranges::lower_bound(*records, request.source, {}, &prepared_flow_screen::source);
        if (found == records->end() || found->source != request.source)
        {
            throw std::invalid_argument("No prepared Flow contract for requested screen");
        }

        auto contract = std::shared_ptr<const prepared_flow_screen>{records, &*found};
        return std::make_unique<stream_load>(options, std::move(request), std::move(contract));
    };
}
} // namespace atlas::ui_content
