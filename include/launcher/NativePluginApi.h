#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCHER_NATIVE_PLUGIN_ABI_VERSION 1

#if defined(_WIN32)
#define LAUNCHER_NATIVE_PLUGIN_EXPORT __declspec(dllexport)
#define LAUNCHER_NATIVE_PLUGIN_CALL __cdecl
#else
#define LAUNCHER_NATIVE_PLUGIN_EXPORT
#define LAUNCHER_NATIVE_PLUGIN_CALL
#endif

LAUNCHER_NATIVE_PLUGIN_EXPORT int LAUNCHER_NATIVE_PLUGIN_CALL launcher_plugin_abi_version(void);
LAUNCHER_NATIVE_PLUGIN_EXPORT int LAUNCHER_NATIVE_PLUGIN_CALL launcher_plugin_request(const char* request_json, char* response_json,
                                                                                      int response_capacity);
LAUNCHER_NATIVE_PLUGIN_EXPORT void LAUNCHER_NATIVE_PLUGIN_CALL launcher_plugin_shutdown(void);

#ifdef __cplusplus
}
#endif
