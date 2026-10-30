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
struct graph_state;
}

class job_id
{
  public:
    job_id() = default;

  private:
    friend class job_graph;
    friend class graph_handle;
    std::shared_ptr<const void> owner_;
    std::size_t index_{0};
};

enum class job_status : std::uint8_t
{
    pending,
    ready,
    running,
    succeeded,
    failed,
    skipped
};

class graph_error : public std::runtime_error
{
  public:
    graph_error(std::string job_name, const std::exception_ptr& cause);
    [[nodiscard]] const std::string& job_name() const noexcept;
    [[nodiscard]] std::exception_ptr cause() const noexcept;

  private:
    std::string job_name_;
    std::exception_ptr cause_;
};

class graph_handle
{
  public:
    graph_handle() = default;
    [[nodiscard]] job_status status(const job_id& job) const;
    // Only directly failed callbacks have an exception; skipped jobs have none.
    [[nodiscard]] std::exception_ptr exception(const job_id& job) const;

  private:
    friend class job_system;
    explicit graph_handle(std::shared_ptr<detail::graph_state> state);
    void validate(const job_id& job) const;
    std::shared_ptr<detail::graph_state> state_;
};

// Build on one coordinator. submit_graph(graph) validates and snapshots the graph;
// later edits or destruction of this builder do not change accepted executions.
class job_graph
{
  public:
    job_graph();
    ~job_graph();
    job_graph(job_graph&&) noexcept;
    job_graph& operator=(job_graph&&) noexcept;
    job_graph(const job_graph&) = delete;
    job_graph& operator=(const job_graph&) = delete;

    job_id add(std::string name, std::function<void()> callback);
    // Repeated edges are harmless. Foreign/default IDs are rejected immediately.
    void precedes(const job_id& prerequisite, const job_id& dependent);

  private:
    friend class job_system;
    struct impl;
    std::unique_ptr<impl> impl_;
    [[nodiscard]] std::shared_ptr<detail::graph_state> snapshot() const;
};
} // namespace atlas::jobs
