#include <atlas/diagnostics/settings.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>

namespace atlas::diagnostics
{
namespace
{
void keys(const YAML::Node& node, std::initializer_list<std::string_view> allowed)
{
    if (!node.IsMap())
    {
        throw std::invalid_argument{"Diagnostics settings must be mappings"};
    }
    std::vector<std::string> seen;
    for (const auto& entry : node)
    {
        const auto key = entry.first.as<std::string>();
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end() ||
            std::find(seen.begin(), seen.end(), key) != seen.end())
        {
            throw std::invalid_argument{"Unknown or duplicate diagnostics key: " + key};
        }
        seen.push_back(key);
    }
}
log_level level(const YAML::Node& node)
{
    static constexpr std::array names{"trace", "debug",    "info", "warning",
                                      "error", "critical", "off"};
    const auto value = node.as<std::string>();
    const auto found = std::find(names.begin(), names.end(), value);
    if (found == names.end())
    {
        throw std::invalid_argument{"Unknown diagnostics level"};
    }
    return static_cast<log_level>(found - names.begin());
}
std::size_t size(const YAML::Node& node)
{
    const auto text = node.as<std::string>();
    std::size_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        throw std::invalid_argument{"Diagnostics capacities must be unsigned integers"};
    }
    return result;
}
logging_options parse_logging(const YAML::Node& log)
{
    logging_options result;
    if (!log)
    {
        return result;
    }
    keys(log, {"level", "asynchronous", "queue_capacity", "max_categories", "max_message_bytes",
               "rotation_bytes", "retained_files", "categories"});
    if (log["level"])
    {
        result.level = level(log["level"]);
    }
    if (log["asynchronous"])
    {
        result.asynchronous = log["asynchronous"].as<bool>();
    }
    for (const auto& [key, destination] :
         std::initializer_list<std::pair<const char*, std::size_t*>>{
             {"queue_capacity", &result.queue_capacity},
             {"max_categories", &result.max_categories},
             {"max_message_bytes", &result.max_message_bytes},
             {"rotation_bytes", &result.rotation_bytes},
             {"retained_files", &result.retained_files}})
    {
        if (log[key])
        {
            *destination = size(log[key]);
        }
    }
    if (const auto categories = log["categories"])
    {
        if (!categories.IsMap())
        {
            throw std::invalid_argument{"Categories must be a mapping"};
        }
        for (const auto& entry : categories)
        {
            result.category_levels.emplace_back(entry.first.as<std::string>(), level(entry.second));
        }
    }
    return result;
}
profiling_options parse_profiling(const YAML::Node& profile)
{
    profiling_options result;
    if (!profile)
    {
        return result;
    }
    keys(profile, {"enabled", "max_events", "max_string_bytes"});
    if (profile["enabled"])
    {
        result.enabled = profile["enabled"].as<bool>();
    }
    if (profile["max_events"])
    {
        result.max_events = size(profile["max_events"]);
    }
    if (profile["max_string_bytes"])
    {
        result.max_string_bytes = size(profile["max_string_bytes"]);
    }
    return result;
}
} // namespace

diagnostics_settings parse_settings(std::string_view yaml)
{
    diagnostics_settings result;
    try
    {
        const auto root = YAML::Load(std::string{yaml});
        if (!root || root.IsNull())
        {
            return result;
        }
        keys(root, {"logging", "profiling"});
        result.logging = parse_logging(root["logging"]);
        result.profiling = parse_profiling(root["profiling"]);
        validate(result.logging);
        validate(result.profiling);
        return result;
    }
    catch (const YAML::Exception& error)
    {
        throw std::invalid_argument{error.what()};
    }
}
} // namespace atlas::diagnostics
