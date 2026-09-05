// Repository: https://github.com/GoldHEN/GoldHEN_Plugins_Repository

#include "Common.h"
#include "plugin_common.h"

#include "assert.h"
#include "logging.h"
#include "types.h"

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <orbis/Remoteplay.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

extern "C" {

attr_public const char* g_pluginName = "no no-remote-play";
attr_public const char* g_pluginDesc = "";
attr_public const char* g_pluginAuth = "kalaposfos";
attr_public u32 g_pluginVersion = 0x00000100; // 1.00
char titleid[16];

// hook_inits and functions
HOOK_INIT(sceRemoteplayProhibit);
int sceRemoteplayProhibit_hook() {
    // LOG_INFO("(STUBBED) called");
    return 0;
}

HOOK_INIT(sceSysmoduleLoadModule);
int sceSysmoduleLoadModule_hook(OrbisSysModule const id) {
    LOG_INFO("called, id: {:#x}", (u16)id);
    s32 ret = HOOK_CONTINUE(sceSysmoduleLoadModule, s32 (*)(OrbisSysModule const), id);
    switch (id) {
    case ORBIS_SYSMODULE_REMOTE_PLAY:
        LOG_INFO("Hooking remoteplay");
        HOOK(sceRemoteplayProhibit);
        break;

    default:
        break;
    }
    return ret;
}

s32 attr_public plugin_load(s32 argc, const char* argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    final_printf("[GoldHEN] Plugin Author(s): %s\n", g_pluginAuth);
    boot_ver();
    struct proc_info procInfo;
    if (!sys_sdk_proc_info(&procInfo)) {
        memcpy(titleid, procInfo.titleid, sizeof(titleid));
        print_proc_info();
    }
    // hooks
    HOOK32(sceSysmoduleLoadModule);
    return 0;
}

s32 attr_public plugin_unload(s32 argc, const char* argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    // unhooks
    UNHOOK(sceSysmoduleLoadModule);
    UNHOOK(sceRemoteplayProhibit);
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void* args) {
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void* args) {
    return 0;
}

} // extern "C"