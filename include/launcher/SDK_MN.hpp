
#ifndef LAUNCHER_SDK_MN_HPP
#define LAUNCHER_SDK_MN_HPP

#include <string>

#ifndef MN_SDK_VERSION
#define MN_SDK_VERSION 2
#endif

#ifndef Null
#define Null 0
#endif

#ifndef False
#define False 0
#endif

#ifndef True
#define True 1
#endif

#define __MN_PLUGIN_IN__ extern "C" __declspec(dllexport)

// Launcher - SDK
namespace mn {

namespace code {

namespace type {
enum : unsigned int {
    Search = 100,
};
} // namespace type

namespace notify {
enum : unsigned int {
    //         param1: Null
    //         param2: Null

    Load = 1000,

    //         param1: Null
    //         param2: Null

    Destroy = 9999,

    //         param1: Null
    //         param2: Null

    Enable = 2010,

    //         param1: Null
    //         param2: Null

    Disable = 2020,

    //         param1: Null
    //         param2: Null

    Open_Option = 3000,

    //         param2: Null

    Search_Init = 5000,
};
} // namespace notify

namespace searchItemAction {
enum : unsigned int {
    Nil = 0,
    Copy = 101,
    NewKey = 102,
    Cmd = 103,
    Run = 104,
};
} // namespace searchItemAction

} // namespace code

using _MN_RESULT = long long;
using PFN_API_FN = void* (*)(void* ctx, unsigned int op, void* p1, void* p2, void* p3, void* p4, void* p5);

// _MN_RESULT _MN_Notify(unsigned int msg, void* param1, void* param2);

using PFN_Notify = _MN_RESULT (*)(unsigned int msg, void* param1, void* param2);

struct _PLUGIN_INFO {
    unsigned int sdk_version = Null;

    const char* name = Null;
    const char* description = Null;

    const char* version = Null;
    const char* author = Null;
    const char* email = Null;
    const char* homepage = Null;

    unsigned int type = Null;

    PFN_Notify fnNotify = Null;
};

struct SEARCH_RESULT_ITEM {
    unsigned int boost = 0;

    bool isWordWrap = false;
    const char* name = Null;
    const char* nameSub = Null;

    const char* icon = Null;
    const unsigned char* iconBuf = Null;
    int iconBufLen = Null;

    unsigned int action = Null;
    const char* param_0 = Null;
    void* param_1 = Null;
};

struct SEARCH_RESULT_LIST {
    SEARCH_RESULT_ITEM* items = Null;
    unsigned int count = Null;
};

// SEARCH_RESULT_LIST _MN_Search_Callback(const char* prefix, const char* key, const char* key_low);

using PFN_SEARCH_CALLBACK = SEARCH_RESULT_LIST (*)(const char* prefix, const char* key, const char* key_low);

// void _MN_Search_Free_Callback();
using PFN_SEARCH_FREE_CALLBACK = void (*)();

struct _SEARCH_INFO {
    const char* name = Null;
    const char* desc = Null;
    const char* icon = Null;
    unsigned int boost = Null;

    const char* keys = Null;

    const char* regex = Null;
    bool isRegexMatch = false;

    bool isGlobalResults = false;

    PFN_SEARCH_CALLBACK fn = Null;
    PFN_SEARCH_FREE_CALLBACK fn_free = Null;
};

inline PFN_API_FN _MN_API_FN = Null; // API Ptr
inline void* _MN_CTX = Null;         // Plugin Context

namespace Api {

enum class APIOPCODE : unsigned int {
    GetVersion = 50,
    GetSelfId = 100,
    GetSelfDir = 110,

    SetCfgItem = 210,
    GetCfgItem = 211,
};

void* Call(unsigned int op, void* param_1 = Null, void* param_2 = Null, void* param_3 = Null, void* param_4 = Null, void* param_5 = Null)
{
    if (!_MN_API_FN || !_MN_CTX) return Null;
    return _MN_API_FN(_MN_CTX, op, param_1, param_2, param_3, param_4, param_5);
}
void* Call(APIOPCODE op, void* param_1 = Null, void* param_2 = Null, void* param_3 = Null, void* param_4 = Null, void* param_5 = Null)
{
    return Call(static_cast<unsigned int>(op), param_1, param_2, param_3, param_4, param_5);
}

std::string GetApiString(APIOPCODE op, size_t bufSize = 256)
{
    std::string buf(bufSize, '\0');
    size_t ws = (size_t)Call(op, buf.data(), (void*)buf.size());
    buf.resize((std::min)(ws, buf.size()));
    return buf;
}

std::string GetVersion()
{
    return GetApiString(APIOPCODE::GetVersion, 50);
}

std::string GetSelfId()
{
    return GetApiString(APIOPCODE::GetSelfId, 512);
}

std::string GetSelfDir()
{
    return GetApiString(APIOPCODE::GetSelfDir, 2048);
}

// @return bool
bool SetCfgItem(const char* key, const char* val)
{
    return (bool)Call(APIOPCODE::SetCfgItem, (void*)key, (void*)val);
}

// @param key

// @return std::string
std::string GetCfgItem(const char* key, const char* delVal = Null, size_t bufSize = 200)
{
    std::string buf(bufSize, '\0');
    size_t ws = (size_t)Call(APIOPCODE::GetCfgItem, (void*)key, (void*)delVal, buf.data(), (void*)buf.size());
    buf.resize((std::min)((size_t)ws, buf.size()));
    return buf;
}

} // namespace Api

} // namespace mn

#endif // LAUNCHER_SDK_MN_HPP
