/**
 * @file
 * @brief Coordinator-built DAG snapshots, stable named failures and node ownership.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace atlas::jobs
{
namespace detail
{
/// @brief Retained immutable graph topology plus synchronized execution results and aggregate
/// completion.
struct graph_state;
} // namespace detail

/// @brief Graph-builder-local identity; default and foreign IDs cannot name accepted nodes.
class job_id
{
  public:
    /// @brief Create an empty builder-local node ID; graph operations reject it until assigned by
    /// add.
    job_id() = default;

  private:
    friend class job_graph;
    friend class graph_handle;
    /// @brief Identity used to reject handles from a different runtime or graph builder.
    std::shared_ptr<const void> owner_;
    /// @brief Builder-local node index validated against the owning graph identity.
    std::size_t index_{0};
};

/// @brief Direct/graph execution states; only terminal publication makes results immutable.
enum class job_status : std::uint8_t
{
    /// @brief Admitted work whose prerequisites have not yet made it eligible.
    pending,
    /// @brief Eligible work waiting for a worker or its origin-thread notification turn.
    ready,
    /// @brief Callback execution has started on its assigned thread.
    running,
    /// @brief Successful terminal result after descendants and scoped cleanup finish.
    succeeded,
    /// @brief Terminal real failure retaining its original cause.
    failed,
    /// @brief Body suppressed because its source/prerequisite outcome did not allow execution.
    skipped,
    /// @brief Body has returned while descendants or scoped cleanup still contribute to completion.
    joining,
    /// @brief Terminal cooperative cancellation, distinct from a real callback failure.
    cancelled
};

/// @brief Named execution failure retaining the original exception as its cause.
class graph_error : public std::runtime_error
{
  public:
    /// @brief Initialize named execution failure retaining the original exception as its cause.
    ///
    /// @param job_name Diagnostic name associated with the stable failure cause.
    /// @param cause Original failure retained without replacing its exception identity.
    graph_error(std::string job_name, const std::exception_ptr& cause);
    /// @brief Return the stable job name associated with this failure wrapper.
    [[nodiscard]] const std::string& job_name() const noexcept;
    /// @brief Return the original exception retained by the named failure wrapper.
    [[nodiscard]] std::exception_ptr cause() const noexcept;

  private:
    /// @brief Owned diagnostic name of the failed job.
    std::string job_name_;
    /// @brief Original exception retained by the named failure wrapper.
    std::exception_ptr cause_;
};

/// @brief Retained sealed-graph execution state for per-node status and original failures.
class graph_handle
{
  public:
    /// @brief Create an empty handle or retain one accepted sealed-graph execution.
    graph_handle() = default;
    /// @brief Read the current execution status; terminal status publishes immutable results.
    ///
    /// @param job Handle/ID belonging to this runtime or graph builder.
    [[nodiscard]] job_status status(const job_id& job) const;
    /// Only directly failed callbacks have an exception; skipped jobs have none.
    [[nodiscard]] std::exception_ptr exception(const job_id& job) const;

  private:
    friend class job_system;
    /// @brief Create an empty handle or retain one accepted sealed-graph execution.
    explicit graph_handle(std::shared_ptr<detail::graph_state> state);
    /// @brief Reject an empty execution handle or an ID from a different graph builder.
    ///
    /// @param job Handle/ID belonging to this runtime or graph builder.
    void validate(const job_id& job) const;
    /// @brief Retained execution result/state used by this handle or adapter.
    std::shared_ptr<detail::graph_state> state_;
};

/// Build on one coordinator. submit_graph(graph) validates and snapshots the graph;
/// later edits or destruction of this builder do not change accepted executions.
class job_graph
{
  public:
    /// @brief Create an empty coordinator-owned graph builder or move an existing builder.
    job_graph();
    /// @brief Release job graph resources and complete its scoped cleanup.
    ~job_graph();
    /// @brief Create an empty coordinator-owned graph builder or move an existing builder.
    job_graph(job_graph&&) noexcept;
    /// @brief Transfer the owned state and leave the moved-from object safely destructible.
    job_graph& operator=(job_graph&&) noexcept;
    /// @brief Create an empty coordinator-owned graph builder or move an existing builder.
    job_graph(const job_graph&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    job_graph& operator=(const job_graph&) = delete;

    /// @brief Add a named callback to the coordinator-owned graph builder and return its
    /// builder-local identity.
    ///
    /// @param name Case-sensitive diagnostic or metric name; metric names must be nonempty bounded
    /// UTF-8.
    /// @param callback User work retained by the graph/legacy submission; it must terminate without
    /// worker-side waits.
    job_id add(std::string name, std::function<void()> callback);
    /// Repeated edges are harmless. Foreign/default IDs are rejected immediately.
    void precedes(const job_id& prerequisite, const job_id& dependent);

  private:
    friend class job_system;
    /// @brief Private implementation state; callers interact through the enclosing public API.
    struct impl;
    /// @brief Owned private implementation storage; destruction follows the enclosing lifecycle
    /// contract.
    std::unique_ptr<impl> impl_;
    /// @brief Validate and copy graph callbacks/topology before any accepted node is published.
    [[nodiscard]] std::shared_ptr<detail::graph_state> snapshot() const;
};
} // namespace atlas::jobs
