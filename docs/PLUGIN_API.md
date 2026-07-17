# Plugin API

This document is the stable contract for Launcher plugins. Both executable process plugins and native DLL plugins use the same JSON request and response schema.

## Quick Start

A plugin directory contains a manifest and an entry file:

```text
plugins/example.echo/
  plugin.json
  example_echo.exe
```

Minimal process manifest:

```json
{
  "id": "example.echo",
  "name": "Echo",
  "version": "1.0.0",
  "entry": "example_echo.exe",
  "type": "process",
  "enabled": true,
  "capabilities": ["search", "actions"]
}
```

Minimal native manifest:

```json
{
  "id": "example.native",
  "name": "Native Example",
  "version": "1.0.0",
  "entry": "example_native.dll",
  "type": "native",
  "enabled": true,
  "capabilities": ["search", "actions"]
}
```

## Manifest

`plugin.json` fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | string | yes | Unique stable plugin id. Use reverse-DNS style when possible. |
| `name` | string | no | Display name. Defaults to `id`. |
| `version` | string | no | Displayed plugin version. |
| `author` | string | no | Displayed author name. |
| `description` | string | no | Description shown in settings. |
| `entry` | string | yes | Executable or DLL path relative to the plugin directory. |
| `type` | string | no | `process` or `native`. Defaults to `process`. |
| `lifecycle` | string | no | Currently `on-demand`. |
| `enabled` | bool | no | Default enabled state for first install. |
| `capabilities` | string[] | no | Any of `search`, `actions`, `events`, `tasks`. |
| `settings` | object[] | no | Settings schema shown in Launcher settings. |

Manifest ids must be unique. Unsupported `type` values and missing `entry` values load the plugin as an error entry in settings.

## Settings Schema

Settings are persisted as strings in Launcher config and passed to every request as `params.settings`.

Supported field types:

- `text`
- `password`
- `bool`
- `checkbox`
- `number`
- `int`
- `float`
- `choice`
- `combo`
- `select`

Field schema:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | string | yes | Stable setting key. |
| `label` | string | no | Display label. Defaults to `id`. |
| `type` | string | no | Input type. Defaults to `text`. |
| `default` | string | no | Default string value. |
| `choices` | string[] | no | Options for choice-like fields. |

Example:

```json
{
  "settings": [
    {
      "id": "mode",
      "label": "Mode",
      "type": "choice",
      "default": "normal",
      "choices": ["normal", "verbose"]
    },
    {
      "id": "dryRun",
      "label": "Dry Run",
      "type": "bool",
      "default": "true"
    }
  ]
}
```

## Process Transport

Process plugins are started per request.

- Working directory: plugin directory.
- Input: one UTF-8 JSON line on stdin.
- Output: one UTF-8 JSON line on stdout.
- Search timeout: short timeout intended for interactive search.
- Run timeout: longer timeout for actions.
- Event timeout: short best-effort timeout.

Process plugins should write only the JSON response line to stdout. Logs should go to stderr or files under `params.cacheDir`.

## Native DLL Transport

Native plugins export a small C ABI declared in `include/launcher/NativePluginApi.h`.

```cpp
extern "C" __declspec(dllexport)
int __cdecl launcher_plugin_abi_version(void);

extern "C" __declspec(dllexport)
int __cdecl launcher_plugin_request(const char* request_json, char* response_json, int response_capacity);

extern "C" __declspec(dllexport)
void __cdecl launcher_plugin_shutdown(void);
```

Rules:

- `launcher_plugin_request` is required.
- `launcher_plugin_abi_version` is optional. If exported, it must return `1`.
- `launcher_plugin_shutdown` is optional and is called before unload.
- Return non-zero from `launcher_plugin_request` on success.
- Write a null-terminated JSON response into `response_json`.
- Do not write more than `response_capacity` bytes.
- Current response buffer size is 1 MiB.
- Calls are serialized by Launcher.

Native plugins run inside Launcher. They must not block for long periods, throw exceptions across the ABI boundary, or keep raw pointers to request/response buffers after returning.

## Request Envelope

Every request uses this envelope:

```json
{
  "id": 1,
  "method": "search",
  "params": {}
}
```

Response envelope:

```json
{
  "id": 1,
  "result": {}
}
```

For compatibility, Launcher also accepts a bare result as the response body. New plugins should return the envelope form.

Common params:

| Field | Type | Description |
| --- | --- | --- |
| `settings` | object | Persisted plugin settings as string values. |
| `pluginDir` | string | Absolute plugin installation directory. |
| `cacheDir` | string | Absolute cache directory created before each request. |
| `tasks` | object[] | Present only when the plugin declares `tasks`. |

## Search

Requires capability: `search`.

Request:

```json
{
  "id": 1,
  "method": "search",
  "params": {
    "query": "abc",
    "limit": 8,
    "settings": {"prefix": "Echo"},
    "pluginDir": "D:/Launcher/plugins/example.echo",
    "cacheDir": "D:/Config/plugin-cache/example.echo"
  }
}
```

Response:

```json
{
  "id": 1,
  "result": [
    {
      "id": "abc",
      "title": "Echo abc",
      "subtitle": "Run the echo action",
      "icon": "extension",
      "score": 100,
      "actions": [
        {"id": "open", "label": "Open"},
        {"id": "copy", "label": "Copy"}
      ]
    }
  ]
}
```

Search result fields:

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | string | yes | Stable result id passed back to `run`. |
| `title` | string | yes | Primary display text. |
| `subtitle` | string | no | Secondary display text. |
| `icon` | string | no | Icon hint. Current samples use values such as `extension`, `task`, `calculator`, and `palette`. |
| `score` | number | no | Higher sorts better among plugin results. |
| `actions` | object[] or string[] | no | Context actions. Defaults to `open`. |

Action object:

```json
{"id": "open", "label": "Open"}
```

String actions are accepted as shorthand:

```json
["open", "copy"]
```

## Run

Requires capability: `actions` or `search`.

Request:

```json
{
  "id": 1,
  "method": "run",
  "params": {
    "resultId": "abc",
    "actionId": "open",
    "settings": {"prefix": "Echo"},
    "pluginDir": "D:/Launcher/plugins/example.echo",
    "cacheDir": "D:/Config/plugin-cache/example.echo"
  }
}
```

Response:

```json
{
  "id": 1,
  "result": {
    "ok": true,
    "message": "Done"
  }
}
```

`message` is optional and may be surfaced by Launcher when useful. `ok` is recommended for plugin-side clarity, but the current host treats a parseable response as a successful transport call.

## Events

Requires capability: `events`.

Request:

```json
{
  "id": 1,
  "method": "event",
  "params": {
    "name": "app.started",
    "fields": {},
    "settings": {},
    "pluginDir": "D:/Launcher/plugins/example.echo",
    "cacheDir": "D:/Config/plugin-cache/example.echo"
  }
}
```

Current event names:

| Event | Description |
| --- | --- |
| `app.started` | Plugins were loaded during app startup. |
| `content.changed` | Launcher content changed. |
| `tasks.changed` | Scheduled tasks changed after task operations. |

Events are best-effort. Launcher ignores event responses.

## Task Snapshot

Requires capability: `tasks`.

When declared, every `search`, `run`, and `event` request includes `params.tasks`.

Task object:

| Field | Type | Description |
| --- | --- | --- |
| `id` | string | Task id. |
| `name` | string | Display name. |
| `enabled` | bool | Whether the task is active. |
| `trigger` | string | Trigger kind. |
| `action` | string | Action kind. |
| `itemId` | string | Launcher item id used by the action. |
| `hour` | number | Hour for daily/weekly triggers. |
| `minute` | number | Minute for daily/weekly triggers. |
| `weekdayMask` | number | Weekly bit mask. |
| `intervalMinutes` | number | Interval trigger period. |
| `onceAt` | number | Unix time for one-shot task. |
| `processName` | string | Process name for process-start trigger. |
| `runMissed` | bool | Whether missed runs are caught up. |
| `runMinimized` | bool | Whether action runs minimized. |
| `retryCount` | number | Retry count. |
| `retryDelaySeconds` | number | Delay between retries. |
| `lastRunAt` | number | Last run Unix time. |
| `nextRunAt` | number | Next run Unix time. |
| `lastSuccess` | bool | Last result. |
| `lastMessage` | string | Last result message. |

Trigger values:

- `once`
- `daily`
- `weekly`
- `interval`
- `app-start`
- `wake-unlock`
- `process-start`

Action values:

- `launch-item`
- `launch-virtual-folder`

## Task Operations

Task changes are requested by returning `taskOperations` from a `run` response. They are ignored in search and event responses.

```json
{
  "id": 1,
  "result": {
    "ok": true,
    "message": "Task updated",
    "taskOperations": [
      {
        "op": "task.create",
        "task": {
          "name": "Run DDNet every morning",
          "enabled": true,
          "trigger": "daily",
          "action": "launch-item",
          "itemId": "ddnet",
          "hour": 9,
          "minute": 0
        }
      },
      {"op": "task.run", "taskId": "task-alpha"},
      {"op": "task.disable", "taskId": "task-beta"}
    ]
  }
}
```

Supported operations:

| Operation | Required fields | Description |
| --- | --- | --- |
| `task.create` | `task` | Creates a task. Missing `id` is generated by Launcher. |
| `task.update` | `taskId` or `task.id`, `task` | Replaces editable task fields and preserves runtime history. |
| `task.delete` | `taskId` | Deletes the task. |
| `task.run` | `taskId` | Runs the task immediately and records history. |
| `task.enable` | `taskId` | Enables the task. |
| `task.disable` | `taskId` | Disables the task. |

Accepted aliases:

- `create`
- `update`
- `delete`
- `run`
- `enable`
- `disable`
- `set-enabled` with boolean `enabled`

Validation and normalization:

- `hour` is clamped to `0..23`.
- `minute` is clamped to `0..59`.
- `weekdayMask` is clamped to `0..127`.
- `intervalMinutes` is at least `1`.
- `retryCount` is clamped to `0..10`.
- `retryDelaySeconds` is clamped to `1..3600`.
- Create/update resets scheduler runtime fields; update preserves history.

After any applied task operation, Launcher emits `tasks.changed`.

## Error Handling

Transport failure, timeout, invalid JSON, unsupported ABI, or missing native symbols cause the request to fail silently from the user's perspective. Search/event failures are skipped. Run failures may surface a generic plugin action failure.

Recommended error response:

```json
{
  "id": 1,
  "result": {
    "ok": false,
    "message": "Human-readable error"
  }
}
```

For native plugins, return non-zero when the response buffer contains valid JSON. Return zero only for hard transport failure.

## Samples

Source samples live under `tests/plugins`:

- `sample_echo`: search/actions/events/settings process plugin.
- `sample_toolbox`: shared implementation for calculator, color, and CLI preview samples.
- `sample_native`: native DLL plugin that reads tasks and returns task operations.

The native sample exports the ABI from `include/launcher/NativePluginApi.h` and can be used as the starting point for DLL plugins.
