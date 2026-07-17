#include "launcher/NativePluginApi.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <string>

using json = nlohmann::json;

namespace {

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool writeResponse(const json& response, char* output, int capacity)
{
    if (!output || capacity <= 0) {
        return false;
    }
    const std::string text = response.dump();
    if (text.size() + 1 > static_cast<size_t>(capacity)) {
        return false;
    }
    std::memcpy(output, text.c_str(), text.size() + 1);
    return true;
}

json search(const json& params)
{
    const std::string query = lower(params.value("query", ""));
    if (query.find("native") == std::string::npos && query.find("task") == std::string::npos) {
        return json::array();
    }
    const json tasks = params.value("tasks", json::array());
    const int count = tasks.is_array() ? static_cast<int>(tasks.size()) : 0;
    return json::array({{{"id", "native-task-tools"},
                         {"title", "Native Task Tools"},
                         {"subtitle", "DLL plugin loaded in-process, tasks visible: " + std::to_string(count)},
                         {"icon", "task"},
                         {"score", 88},
                         {"actions", json::array({{{"id", "clone-first-task"}, {"label", "Clone first task"}},
                                                  {{"id", "run-first-task"}, {"label", "Run first task"}},
                                                  {{"id", "disable-first-task"}, {"label", "Disable first task"}}})}}});
}

json run(const json& params)
{
    const std::string action = params.value("actionId", "open");
    const json tasks = params.value("tasks", json::array());
    if (!tasks.is_array() || tasks.empty()) {
        return json{{"ok", true}, {"message", "no tasks available"}};
    }

    const json& first = tasks.front();
    const std::string firstId = first.value("id", "");
    if (action == "run-first-task") {
        return json{{"ok", true}, {"taskOperations", json::array({{{"op", "task.run"}, {"taskId", firstId}}})}};
    }
    if (action == "disable-first-task") {
        return json{{"ok", true}, {"taskOperations", json::array({{{"op", "task.disable"}, {"taskId", firstId}}})}};
    }
    if (action == "clone-first-task" || action == "open") {
        json task = first;
        task.erase("id");
        task["name"] = first.value("name", "Task") + std::string(" (native copy)");
        task["enabled"] = false;
        return json{{"ok", true},
                    {"message", "created disabled task copy"},
                    {"taskOperations", json::array({{{"op", "task.create"}, {"task", task}}})}};
    }
    return json{{"ok", true}, {"message", "unknown action"}};
}

json event(const json& params)
{
    return json{{"ok", true}, {"message", "event " + params.value("name", "")}};
}

json handleRequest(const json& request)
{
    const std::string method = request.value("method", "");
    const json params = request.value("params", json::object());
    if (method == "search") {
        return search(params);
    }
    if (method == "run") {
        return run(params);
    }
    if (method == "event") {
        return event(params);
    }
    return json{{"error", "unknown method"}};
}

} // namespace

int LAUNCHER_NATIVE_PLUGIN_CALL launcher_plugin_abi_version(void)
{
    return LAUNCHER_NATIVE_PLUGIN_ABI_VERSION;
}

int LAUNCHER_NATIVE_PLUGIN_CALL launcher_plugin_request(const char* requestJson, char* responseJson, int responseCapacity)
{
    try {
        const json request = json::parse(requestJson ? requestJson : "{}");
        const json response{{"id", request.value("id", 0)}, {"result", handleRequest(request)}};
        return writeResponse(response, responseJson, responseCapacity) ? 1 : 0;
    } catch (const std::exception& ex) {
        return writeResponse(json{{"error", ex.what()}}, responseJson, responseCapacity) ? 1 : 0;
    }
}

void LAUNCHER_NATIVE_PLUGIN_CALL launcher_plugin_shutdown(void) {}
