/** @file @brief Compare XML and prepared construction from equivalent in-memory assets. */
#include <algorithm>
#include <atlas/ui_content/prepared.hpp>
#include <chrono>
#include <cstdio>
#include <iostream>

namespace atlas::ui_content
{
namespace
{
/// @brief Reuse one UI asset across distinct menus to isolate the graph and contract costs.
flow_document menu_graph(assets::asset_ref source)
{
    flow_document graph;
    graph.regions.push_back({"main", "menu_0", ""});
    for (std::size_t index = 0; index < 32; ++index)
    {
        const auto name = "menu_" + std::to_string(index);
        graph.screens.push_back({name, "main", source});
        if (index != 0)
        {
            graph.transitions.push_back({.id = "go_" + std::to_string(index),
                                         .from = graph.screens.at(index - 1).id,
                                         .to = name,
                                         .operation = flow_operation::replace});
        }
    }

    return graph;
}

/// @brief Keep the comparison reproducible and outside filesystem and GPU timing.
class benchmark
{
  public:
    /// @brief Build one shared shape template and an authored menu of bounded size.
    explicit benchmark(std::size_t elements)
    {
        assets::catalog_document data{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
        material_template shape{};
        shape.defaults.kind = ui::material_kind::shape;
        shape.defaults.radius = {0.25F, 0.25F, 0.25F, 0.25F};
        const auto material =
            add(data, "material.xml", assets::asset_type::material, write_material_xml(shape));
        document document_value;
        document_value.elements.resize(elements);
        document_value.elements.front().id = "root";
        for (std::size_t i = 1; i < elements; ++i)
        {
            auto& node = document_value.elements.at(i);
            node.id = "panel_" + std::to_string(i);
            node.parent = "root";
            node.paint.fill = true;
            node.material = material;
        }

        xml_ = write_xml(document_value);
        const auto source = add(data, "menu.xml", assets::asset_type::ui, xml_);
        flow_xml_ = write_flow_xml(menu_graph(source));
        const auto flow_source = add(data, "flow.xml", assets::asset_type::flow, flow_xml_);
        mount_ = std::make_unique<streaming::catalog_streamer>(
            jobs_, assets::catalog::compile(std::move(data)), std::move(storage_));
        prepared_ = encode_prepared(prepare_view(source, {*mount_}));
        flow_prepared_ = encode_prepared(prepare_flow(flow_source, {*mount_}));
    }

    /// @brief Verify semantic output and report decode plus construction separately.
    void run(std::size_t samples)
    {
        const construction options{*mount_};
        const auto read_xml = [&]
        {
            return parse_xml({xml_});
        };
        const auto read_prepared = [&]
        {
            return decode_prepared(prepared_);
        };
        const auto from_xml = [&]
        {
            return instance::build(read_xml(), options);
        };
        const auto from_prepared = [&]
        {
            return build_prepared(read_prepared(), options);
        };
        const auto old = from_xml();
        const auto cooked = from_prepared();
        if (write_xml(read_prepared().view) != write_xml(read_xml()) ||
            old->tree().material(old->find("panel_1")) !=
                cooked->tree().material(cooked->find("panel_1")))
        {
            throw std::runtime_error("Prepared benchmark composition mismatch");
        }

        std::cout << "xml_bytes=" << xml_.size() << " prepared_bytes=" << prepared_.size() << '\n';
        measure("xml_decode", read_xml, samples);
        measure("prepared_decode", read_prepared, samples);
        measure("xml_decode_and_build", from_xml, samples);
        measure("prepared_decode_and_build", from_prepared, samples);
        run_flow(options, samples);
    }

  private:
    /// @brief Compare equally validated graph decodes and manager construction before screen I/O.
    void run_flow(const construction& options, std::size_t samples) const
    {
        const auto authored = [&]
        {
            return parse_flow_xml({flow_xml_});
        };
        const auto prepared = [&]
        {
            return decode_prepared_flow(flow_prepared_);
        };
        if (write_flow_xml(prepared().graph) != write_flow_xml(authored()))
        {
            throw std::runtime_error("Prepared Flow benchmark mismatch");
        }

        const auto authored_manager = [&]
        {
            return std::make_unique<flow_manager>(authored(), make_flow_loader(options));
        };
        const auto prepared_manager = [&]
        {
            return build_prepared_flow(prepared(), options);
        };
        std::cout << "flow_xml_bytes=" << flow_xml_.size()
                  << " flow_prepared_bytes=" << flow_prepared_.size() << '\n';
        measure("flow_xml_decode", authored, samples);
        measure("flow_prepared_decode", prepared, samples);
        measure("flow_xml_decode_and_build", authored_manager, samples);
        measure("flow_prepared_decode_and_build", prepared_manager, samples);
    }

    /// @brief Compare identical material/view IDs across the two construction paths.
    assets::asset_ref add(assets::catalog_document& data, const char* name, assets::asset_type type,
                          const std::string& bytes)
    {
        const assets::asset_ref source{assets::allocate_id(data), type};
        data.records.push_back({source, {"benchmark", name}, 1, bytes.size()});
        storage_.sources.emplace(
            name, streaming::memory_asset{
                      std::make_shared<const streaming::asset_bytes>(bytes.begin(), bytes.end())});
        storage_.routes.push_back({source, 1, name, 0});
        return source;
    }

    /// @brief Record warmed P50 and P95 without treating them as correctness thresholds.
    template <class Run>
    static void measure(std::string_view name, const Run& run, std::size_t count)
    {
        for (int warmup = 0; warmup < 3; ++warmup)
        {
            (void)run();
        }

        using clock = std::chrono::steady_clock; ///< Monotonic interval measurement.
        std::vector<double> samples;
        samples.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto start = clock::now();
            const auto result = run();
            samples.push_back(
                std::chrono::duration<double, std::micro>(clock::now() - start).count());
            (void)result;
        }

        std::ranges::sort(samples);
        std::cout << name << " p50_us=" << samples.at(count / 2)
                  << " p95_us=" << samples.at((count - 1) * 95 / 100) << '\n';
    }

    jobs::job_system jobs_{2};         ///< Shared workers for authorized in-memory material access.
    streaming::storage_mount storage_; ///< Candidate raw catalog mount.
    std::unique_ptr<streaming::catalog_streamer> mount_; ///< Typed source for both paths.
    std::string xml_;                                    ///< Authored UI source bytes.
    std::vector<std::byte> prepared_;                    ///< Equivalent typed binary bytes.
    std::string flow_xml_;                 ///< Authored graph used for the Flow comparison.
    std::vector<std::byte> flow_prepared_; ///< Graph and exact revisions under test.
};
} // namespace
} // namespace atlas::ui_content

/// @brief Run the smoke-sized or measured deterministic scenario.
int main(int argc, char** argv)
{
    try
    {
        const bool smoke = argc == 2 && std::string_view{argv[1]} == "--smoke";
        atlas::ui_content::benchmark probe{smoke ? std::size_t{16} : std::size_t{512}};
        probe.run(smoke ? 1U : 40U);
        return 0;
    }
    catch (const std::exception& error)
    {
        (void)std::fputs(error.what(), stderr);
        (void)std::fputc('\n', stderr);
        return 1;
    }
    catch (...)
    {
        (void)std::fputs("Unclassified prepared UI benchmark failure\n", stderr);
        return 1;
    }
}
