/**
 * @file
 * @brief Shared owner-thread Lua domains with catalog sources and validated instances.
 */
#pragma once
#include <atlas/scripting/session.hpp>

namespace atlas::scripting
{
/// @brief Immutable revision supplied by an external catalog/streamer; no file access is implied.
struct source_asset
{
    std::string identity;       ///< Stable catalog identity within the domain.
    std::uint64_t revision = 0; ///< Strictly increasing nonzero revision per identity.
    std::string source_name;    ///< Stable authoring path prefixed with Lua's file marker.
    std::string text;           ///< Owned text bytes supplied by the storage boundary.
};

/// @brief Bind a behavior revision to one named model context without a per-model native class.
struct behavior_spec
{
    std::string source;  ///< Catalog identity of the behavior entry source.
    std::string context; ///< Registered ViewModel name.
};

/// @brief Nonreused domain/registration identity and generation checked before Lua access.
struct instance_handle
{
    std::uint64_t domain = 0;     ///< Nonzero domain lifetime identity.
    std::uint64_t sequence = 0;   ///< Nonreused registration identity.
    std::uint64_t generation = 0; ///< Reload generation; zero is never live.
    bool operator==(const instance_handle&) const = default; ///< All identity components matter.
};

/// @brief Owned native work retains its original instance identity and weak result transport.
struct instance_work
{
    instance_handle instance; ///< Original instance/generation; never a Lua reference.
    work_request work; ///< Worker-safe values and cancellation from the originating instance.
};

/// @brief Coordinator-to-domain ingress; no Lua object crosses an interpreter boundary.
struct domain_message
{
    instance_handle destination;        ///< Exact domain, instance and generation.
    std::uint64_t sequence = 0;         ///< Strictly increasing ingress sequence.
    std::uint64_t frame = 0;            ///< Exact admitted logical frame.
    std::string handler;                ///< Receiver event vocabulary.
    std::vector<mvvm::value> arguments; ///< Owned typed scalars/snapshots.
};

/// @brief Domain observations; Lua bytes aggregate all instances, excluding host metadata.
struct domain_statistics
{
    std::uint64_t frame = 0;        ///< Last admitted logical frame.
    std::size_t instances = 0;      ///< Retained behavior registrations.
    std::size_t enabled = 0;        ///< Active behavior registrations.
    std::size_t interpreters = 0;   ///< At most one physical interpreter.
    std::size_t lua_bytes = 0;      ///< Requested live Lua allocation bytes.
    std::size_t lua_peak_bytes = 0; ///< Peak aggregate Lua allocation bytes.
};

/// @brief Inexpensive synchronous capability implemented by a consuming project/add-on.
class synchronous_service
{
  public:
    virtual ~synchronous_service() = default; ///< Release on the owner after instance retirement.
    virtual std::vector<mvvm::value>
    invoke(std::span<const mvvm::value>
               arguments) = 0; ///< Return owned scalars; heavy work belongs in requests.
};

/// @brief Exact scalar protocol around a project-provided synchronous capability.
struct service_definition
{
    std::string name;                                    ///< Stable capability identity.
    std::vector<mvvm::value_type> parameters;            ///< At most 32 exact argument types.
    std::vector<mvvm::value_type> results;               ///< At most 32 exact result types.
    std::shared_ptr<synchronous_service> implementation; ///< Retained owner-thread implementation.
};

/// @brief Independent UI/application lifetime; one interpreter, many isolated behavior contexts.
class domain
{
  public:
    explicit domain(
        session_limits limits = {}, std::shared_ptr<std::pmr::memory_resource> memory = {},
        diagnostics::services destinations = {}); ///< Retain bounded shared infrastructure.
    ~domain();                      ///< Shut down on the coordinator, outside event dispatch.
    domain(const domain&) = delete; ///< One interpreter owner.
    domain& operator=(const domain&) = delete; ///< No shared mutable domain aliases.
    void bind_context(
        std::string name,
        std::shared_ptr<mvvm::view_model> model); ///< Register before creating dependent instances.
    void register_source(source_asset source); ///< Admit immutable monotonically versioned bytes.
    void register_service(service_definition service); ///< Register a typed project capability.
    instance_handle
    create(behavior_spec spec); ///< Append one isolated instance in registration order.
    instance_handle reload(instance_handle instance); ///< Transactional fresh script state; retire
                                                      ///< old handles/results.
    void
    remove(instance_handle instance); ///< Retire subscriptions/work and the instance environment.
    void set_enabled(instance_handle instance,
                     bool enabled);        ///< Disable retires subscriptions and pending work.
    void begin_frame(std::uint64_t frame); ///< Models must already have the same healthy frame.
    std::vector<mvvm::value>
    call(instance_handle instance, std::string_view handler,
         std::span<const mvvm::value> arguments = {},
         std::span<const mvvm::value_type> result_types =
             {}); ///< Route one generation-checked event through the Lua director.
    void dispatch(std::string_view handler,
                  std::span<const mvvm::value> arguments =
                      {}); ///< Visit enabled instances in deterministic registration order.
    void synchronize();    ///< Apply script listener changes after all model flushes.
    std::vector<instance_work>
    take_requests();        ///< Transfer value-only native requests with their owner identity.
    void deliver_results(); ///< Accept ready per-instance ordered prefixes on the coordinator.
    void enqueue(domain_message message); ///< Admit owner-thread ingress for the current frame.
    void deliver_messages(); ///< Dispatch in sequence order, dropping retired/disabled targets.
    void attach_debugger(
        instance_handle instance,
        debugger_options options); ///< Debug a selected execution context in this domain.
    void detach_debugger(
        instance_handle instance); ///< Retire selected context's development attachment.
    statistics
    inspect(instance_handle instance) const; ///< Observe one validated execution context.
    domain_statistics inspect() const;       ///< Observe aggregate interpreter usage.
    void shutdown(); ///< Idempotently cancel instances; independent domains remain active.
  private:
    class impl; ///< Catalog, instance ordering and the shared physical interpreter.
    std::unique_ptr<impl> impl_; ///< No dependency on UI, renderer, storage or scheduler.
};
} // namespace atlas::scripting
