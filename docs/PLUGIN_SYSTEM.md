# Plugin System

Launcher plugins extend the local launcher without turning the main application into a general scripting host. The system is intentionally small: plugins can contribute search results, result actions, lifecycle events, settings, and scheduled-task operations. They cannot draw arbitrary ImGui UI or mutate internal state directly.

For the concrete JSON contract, manifest fields, request/response schema, task API, and native DLL ABI, see [PLUGIN_API.md](PLUGIN_API.md).

## Goals

- Keep the launcher responsive during wake, search, and result execution.
- Let small local tools integrate with search and actions without rebuilding Launcher.
- Support both crash-isolated executable plugins and faster in-process DLL plugins.
- Keep plugin capabilities explicit so users can see what a plugin is allowed to do.
- Make plugin state local, portable, and easy to inspect.

## Plugin Locations

Launcher scans these roots:

```text
<launcher executable directory>/plugins/<plugin-id>/
<config directory>/plugins/<plugin-id>/
```

Each plugin directory must contain `plugin.json`. The directory name does not have to match the manifest id, but unique manifest ids are required. If two plugin folders use the same id, the first one found is used.

Builds package sample plugins into the executable directory:

- `sample-echo`: process plugin for search, actions, events, and settings.
- `sample-calc`: process plugin for calculator-style search.
- `sample-color`: process plugin for color parsing.
- `sample-cli`: process plugin for CLI-like search/actions.
- `sample-native`: native DLL plugin for task API smoke testing.

## Transports

Launcher supports two plugin transports with the same API surface.

`process` plugins are executables. Launcher starts the process on demand, sends one JSON request line to stdin, reads one JSON response line from stdout, and then lets the process exit or terminates it after a timeout. This mode is more isolated and is the default choice for external tools.

`native` plugins are DLLs loaded with `LoadLibraryW`. Launcher calls a C ABI entry point and passes the same JSON request shape used by process plugins. This mode avoids process startup overhead and can keep plugin state in memory, but the DLL runs inside Launcher. Crashes, blocking calls, and leaks affect the main process.

Both transports receive the same methods, settings, `pluginDir`, `cacheDir`, events, search/action protocol, and task API. Differences are transport-level:

- Process plugins run with the plugin directory as the working directory.
- Native plugins should use `params.pluginDir` to locate sidecar files.
- Process plugins can be timed out and killed.
- Native plugin calls cannot be safely killed; they must return quickly.
- Process plugin failures are isolated to the child process.
- Native plugin failures can destabilize Launcher.

## Capabilities

Capabilities are declared in `plugin.json` and gate what Launcher sends or accepts.

- `search`: the plugin can receive `search` requests and return search results.
- `actions`: the plugin can receive `run` requests for result actions.
- `events`: the plugin can receive `event` requests.
- `tasks`: the plugin receives scheduled-task snapshots and can return task operations from `run`.

Settings do not require a capability; a plugin exposes settings by declaring a `settings` schema in the manifest.

## Lifecycle

Current lifecycle is `on-demand`.

- Process plugins are started separately for each request.
- Native plugins are loaded on first request and kept loaded until plugins are reloaded or Launcher exits.
- Native plugins may export `launcher_plugin_shutdown` for cleanup before unload.

Plugin reload unloads native DLLs, reloads manifests, reapplies persisted enable/settings state, and refreshes the installed plugin list.

## State And Cache

Launcher persists plugin enable state and settings in the main config file. Plugin code should store its own generated data under `params.cacheDir`; Launcher creates this directory before calling either process or native plugins.

`params.pluginDir` points at the plugin installation directory. Use it for read-only bundled resources such as templates, helper scripts, or static metadata.

## Events

Current events are:

- `app.started`: emitted when plugins are loaded during app startup.
- `content.changed`: emitted after launcher content changes.
- `tasks.changed`: emitted after task operations change the scheduled-task list.

Events are best-effort. Slow or failing plugins are ignored.

## Task Integration

The task API is intentionally request-based. Plugins do not receive direct access to the scheduler. A plugin with the `tasks` capability receives a task snapshot in request params and may return `taskOperations` from a `run` response.

Launcher validates and applies supported operations:

- `task.create`
- `task.update`
- `task.delete`
- `task.run`
- `task.enable`
- `task.disable`

After task operations are applied, Launcher emits `tasks.changed`.

## Performance Rules

- Search handlers must be fast. Process plugin search requests use a short timeout.
- Native plugins must avoid blocking the UI/search path because they run in-process.
- Expensive indexing should happen outside search requests and cache results under `cacheDir`.
- Response JSON should stay compact. Native plugin responses currently have a 1 MiB buffer.

## Security Model

Plugins are local code. Launcher does not sandbox them beyond process separation for `process` plugins. Users should install plugins only from trusted sources.

Native plugins are especially sensitive because they run inside the Launcher process. Prefer `process` plugins for untrusted or complex integrations, and reserve `native` plugins for small, performance-sensitive integrations that are easy to audit.
