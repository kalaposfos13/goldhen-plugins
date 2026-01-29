// Repository: https://github.com/GoldHEN/GoldHEN_Plugins_Repository

#include "Common.h"
#include "plugin_common.h"

#include "assert.h"
#include "logging.h"
#include "types.h"

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <orbis/Http.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include "np_manager.h"
#include "np_types.h"

using namespace Libraries::Np::NpManager;
using namespace Libraries::Np;

namespace Config {
std::string getUserName() {
    int u;
    sceUserServiceGetInitialUser(&u);
    char n[32];
    sceUserServiceGetUserName(u, n, sizeof(n));
    return std::string(n);
}
} // namespace Config

std::string ReplaceHost(std::string url, bool force_http = true) {
    // LOG_INFO("Url: {}", url);
    // return url;

    std::string new_host = "bbnet.yahargul.info";

    std::string separator = "://";
    u64 protocol_pos = url.find(separator);

    u64 host_start = 0;

    if (protocol_pos != std::string::npos) {
        host_start = protocol_pos + separator.length();
    }

    u64 host_end = url.find_first_of("/:?#", host_start);
    if (host_end == std::string::npos) {
        host_end = url.length();
    }

    url.replace(host_start, host_end - host_start, new_host);

    if (force_http && true) {
        if (protocol_pos != std::string::npos) {

            url.replace(0, protocol_pos, "http");
        } else {

            url.insert(0, "http://");
        }
    }

    // LOG_INFO("Replaced URL host, new URL: {}", url);

    return url;
}

static bool g_signed_in = true;
static s32 g_active_requests = 0;
static std::mutex g_request_mutex;

// Internal types for storing request-related information
enum class NpRequestState {
    None = 0,
    Ready = 1,
    Aborted = 2,
    Complete = 3,
};

struct NpRequest {
    NpRequestState state;
    bool async;
    s32 result;
};

static std::vector<NpRequest> g_requests;

s32 CreateNpRequest(bool async) {
    if (g_active_requests == ORBIS_NP_MANAGER_REQUEST_LIMIT) {
        return ORBIS_NP_ERROR_REQUEST_MAX;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = 0;
    while (req_index < g_requests.size()) {
        // Find first nonexistant request
        if (g_requests[req_index].state == NpRequestState::None) {
            // There is no request at this index, set the index to ready then break.
            g_requests[req_index].state = NpRequestState::Ready;
            g_requests[req_index].async = async;
            break;
        }
        req_index++;
    }

    if (req_index == g_requests.size()) {
        // There are no requests to replace.
        NpRequest new_request{NpRequestState::Ready, async, 0};
        g_requests.emplace_back(new_request);
    }

    // Offset by one, first returned ID is 0x20000001
    g_active_requests++;
    return req_index + ORBIS_NP_MANAGER_REQUEST_ID_OFFSET + 1;
}

extern "C" {

attr_public const char* g_pluginName = "host override";
attr_public const char* g_pluginDesc = "";
attr_public const char* g_pluginAuth = "kalaposfos";
attr_public u32 g_pluginVersion = 0x00000100; // 1.00
char titleid[16];

HOOK_INIT(sceNpCreateRequest);
HOOK_INIT(sceNpCreateAsyncRequest);
HOOK_INIT(sceNpCheckNpAvailability);
HOOK_INIT(sceNpCheckNpAvailabilityA);
HOOK_INIT(sceNpCheckNpReachability);
HOOK_INIT(sceNpCheckPlus);
HOOK_INIT(sceNpGetAccountLanguage);
HOOK_INIT(sceNpGetAccountLanguageA);
HOOK_INIT(sceNpGetParentalControlInfo);
HOOK_INIT(sceNpGetParentalControlInfoA);
HOOK_INIT(sceNpAbortRequest);
HOOK_INIT(sceNpWaitAsync);
HOOK_INIT(sceNpPollAsync);
HOOK_INIT(sceNpDeleteRequest);
HOOK_INIT(sceNpGetAccountCountry);
HOOK_INIT(sceNpGetAccountCountryA);
HOOK_INIT(sceNpGetAccountDateOfBirth);
HOOK_INIT(sceNpGetAccountDateOfBirthA);
HOOK_INIT(sceNpGetGamePresenceStatus);
HOOK_INIT(sceNpGetGamePresenceStatusA);
HOOK_INIT(sceNpGetAccountId);
HOOK_INIT(sceNpGetAccountIdA);
HOOK_INIT(sceNpGetNpId);
HOOK_INIT(sceNpGetOnlineId);
HOOK_INIT(sceNpGetNpReachabilityState);
HOOK_INIT(sceNpGetState);
HOOK_INIT(sceNpHasSignedUp);
HOOK_INIT(sceNpCheckCallback);
HOOK_INIT(sceNpCheckCallbackForLib);

s32 PS4_SYSV_ABI sceNpCreateRequest_hook() {
    LOG_DEBUG("called");
    return CreateNpRequest(false);
}

s32 PS4_SYSV_ABI sceNpCreateAsyncRequest_hook(const OrbisNpCreateAsyncRequestParameter* param) {
    LOG_DEBUG("called");
    if (param == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    if (param->size != sizeof(OrbisNpCreateAsyncRequestParameter)) {
        return ORBIS_NP_ERROR_INVALID_SIZE;
    }

    return CreateNpRequest(true);
}

s32 PS4_SYSV_ABI sceNpCheckNpAvailability_hook(s32 req_id, OrbisNpOnlineId* online_id) {
    if (online_id == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_requests[req_index];
    if (request.state == NpRequestState::Complete) {
        request.result = ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpRequestState::Aborted) {
        request.result = ORBIS_NP_ERROR_ABORTED;
        return ORBIS_NP_ERROR_ABORTED;
    }

    request.state = NpRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = {:#x}, is_async = {}", req_id, request.async);

    request.result = ORBIS_OK;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpCheckNpAvailabilityA_hook(s32 req_id, OrbisUserServiceUserId user_id) {
    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_requests[req_index];
    if (request.state == NpRequestState::Complete) {
        request.result = ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpRequestState::Aborted) {
        request.result = ORBIS_NP_ERROR_ABORTED;
        return ORBIS_NP_ERROR_ABORTED;
    }

    request.state = NpRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = {:#x}, is_async = {}", req_id, request.async);

    request.result = ORBIS_OK;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpCheckNpReachability_hook(s32 req_id, OrbisUserServiceUserId user_id) {
    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_requests[req_index];
    if (request.state == NpRequestState::Complete) {
        request.result = ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpRequestState::Aborted) {
        request.result = ORBIS_NP_ERROR_ABORTED;
        return ORBIS_NP_ERROR_ABORTED;
    }

    request.state = NpRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = {:#x}, is_async = {}", req_id, request.async);

    request.result = ORBIS_OK;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpCheckPlus_hook(s32 req_id, const OrbisNpCheckPlusParameter* param,
                                     OrbisNpCheckPlusResult* result) {

    if (req_id == 0 || param == nullptr || result == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    if (param->size != sizeof(OrbisNpCheckPlusParameter)) {
        return ORBIS_NP_ERROR_INVALID_SIZE;
    }

    if (param->features < 1 || param->features > 3) {
        // TODO: If compiled SDK version is greater or equal to fw 3.50,
        // error if param->features != 1 instead.
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_requests[req_index];
    if (request.state == NpRequestState::Complete) {
        request.result = ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpRequestState::Aborted) {
        request.result = ORBIS_NP_ERROR_ABORTED;
        return ORBIS_NP_ERROR_ABORTED;
    }

    request.state = NpRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = {:#x}, is_async = {}, param.features = {:#x}", req_id,
              request.async, param->features);

    // For now, set authorized to true to signal PS+ access.
    result->authorized = true;

    request.result = ORBIS_OK;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetAccountLanguage_hook(s32 req_id, OrbisNpOnlineId* online_id,
                                              OrbisNpLanguageCode* language) {
    if (online_id == nullptr || language == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_requests[req_index];
    if (request.state == NpRequestState::Complete) {
        request.result = ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpRequestState::Aborted) {
        request.result = ORBIS_NP_ERROR_ABORTED;
        return ORBIS_NP_ERROR_ABORTED;
    }

    request.state = NpRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = {:#x}, is_async = {}", req_id, request.async);

    std::memset(language, 0, sizeof(OrbisNpLanguageCode));

    request.result = ORBIS_OK;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetAccountLanguageA_hook(s32 req_id, OrbisUserServiceUserId user_id,
                                               OrbisNpLanguageCode* language) {
    if (language == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_requests[req_index];
    if (request.state == NpRequestState::Complete) {
        request.result = ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpRequestState::Aborted) {
        request.result = ORBIS_NP_ERROR_ABORTED;
        return ORBIS_NP_ERROR_ABORTED;
    }

    request.state = NpRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = {:#x}, user_id = {}, is_async = {}", req_id, user_id,
              request.async);

    std::memset(language, 0, sizeof(OrbisNpLanguageCode));

    request.result = ORBIS_OK;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetParentalControlInfo_hook(s32 req_id, OrbisNpOnlineId* online_id, s8* age,
                                                  OrbisNpParentalControlInfo* info) {
    if (online_id == nullptr || age == nullptr || info == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_requests[req_index];
    if (request.state == NpRequestState::Complete) {
        request.result = ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpRequestState::Aborted) {
        request.result = ORBIS_NP_ERROR_ABORTED;
        return ORBIS_NP_ERROR_ABORTED;
    }

    request.state = NpRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = {:#x}, is_async = {}", req_id, request.async);

    // TODO: Add to config?
    *age = 13;
    std::memset(info, 0, sizeof(OrbisNpParentalControlInfo));

    request.result = ORBIS_OK;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetParentalControlInfoA_hook(s32 req_id, OrbisUserServiceUserId user_id,
                                                   s8* age, OrbisNpParentalControlInfo* info) {
    if (age == nullptr || info == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_requests[req_index];
    if (request.state == NpRequestState::Complete) {
        request.result = ORBIS_NP_ERROR_INVALID_ARGUMENT;
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpRequestState::Aborted) {
        request.result = ORBIS_NP_ERROR_ABORTED;
        return ORBIS_NP_ERROR_ABORTED;
    }

    request.state = NpRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = {:#x}, user_id = {}, is_async = {}", req_id, user_id,
              request.async);

    // TODO: Add to config?
    *age = 13;
    std::memset(info, 0, sizeof(OrbisNpParentalControlInfo));

    request.result = ORBIS_OK;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpAbortRequest_hook(s32 req_id) {
    LOG_DEBUG("called req_id = {:#x}", req_id);

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    if (g_requests[req_index].state == NpRequestState::Complete) {
        // If the request is already complete, abort is ignored.
        return ORBIS_OK;
    }

    g_requests[req_index].state = NpRequestState::Aborted;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpWaitAsync_hook(s32 req_id, s32* result) {
    if (result == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    if (!g_requests[req_index].async || g_requests[req_index].state == NpRequestState::Ready) {
        return ORBIS_NP_ERROR_INVALID_ID;
    }

    // Since we're not actually performing any sort of network request here,
    // we can just set result based on the request and return.
    *result = g_requests[req_index].result;
    LOG_WARNING("called req_id = {:#x}, returning result = {:#x}", req_id,
                static_cast<u32>(*result));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpPollAsync_hook(s32 req_id, s32* result) {
    if (result == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    if (!g_requests[req_index].async || g_requests[req_index].state == NpRequestState::Ready) {
        return ORBIS_NP_ERROR_INVALID_ID;
    }

    // Since we're not actually performing any sort of network request here,
    // we can just set result based on the request and return.
    *result = g_requests[req_index].result;
    LOG_WARNING("called req_id = {:#x}, returning result = {:#x}", req_id,
                static_cast<u32>(*result));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpDeleteRequest_hook(s32 req_id) {
    LOG_DEBUG("called req_id = {:#x}", req_id);

    std::scoped_lock lk{g_request_mutex};

    s32 req_index = req_id - ORBIS_NP_MANAGER_REQUEST_ID_OFFSET - 1;
    if (g_active_requests == 0 || g_requests.size() <= req_index ||
        g_requests[req_index].state == NpRequestState::None) {
        return ORBIS_NP_ERROR_REQUEST_NOT_FOUND;
    }

    g_active_requests--;
    g_requests[req_index].state = NpRequestState::None;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetAccountCountry_hook(OrbisNpOnlineId* online_id,
                                             OrbisNpCountryCode* country_code) {
    if (online_id == nullptr || country_code == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }
    std::memset(country_code, 0, sizeof(OrbisNpCountryCode));
    // TODO: get NP country code from config
    std::memcpy(country_code->country_code, "us", 2);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetAccountCountryA_hook(OrbisUserServiceUserId user_id,
                                              OrbisNpCountryCode* country_code) {
    if (country_code == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }
    std::memset(country_code, 0, sizeof(OrbisNpCountryCode));
    // TODO: get NP country code from config
    std::memcpy(country_code->country_code, "us", 2);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetAccountDateOfBirth_hook(OrbisNpOnlineId* online_id,
                                                 OrbisNpDate* date_of_birth) {
    if (online_id == nullptr || date_of_birth == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    // TODO: maybe add to config?
    date_of_birth->day = 1;
    date_of_birth->month = 1;
    date_of_birth->year = 2000;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetAccountDateOfBirthA_hook(OrbisUserServiceUserId user_id,
                                                  OrbisNpDate* date_of_birth) {
    if (date_of_birth == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    // TODO: maybe add to config?
    date_of_birth->day = 1;
    date_of_birth->month = 1;
    date_of_birth->year = 2000;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetGamePresenceStatus_hook(OrbisNpOnlineId* online_id,
                                                 OrbisNpGamePresenseStatus* game_status) {
    if (online_id == nullptr || game_status == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    *game_status =
        g_signed_in ? OrbisNpGamePresenseStatus::Online : OrbisNpGamePresenseStatus::Offline;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetGamePresenceStatusA_hook(OrbisUserServiceUserId user_id,
                                                  OrbisNpGamePresenseStatus* game_status) {
    if (game_status == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    *game_status =
        g_signed_in ? OrbisNpGamePresenseStatus::Online : OrbisNpGamePresenseStatus::Offline;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetAccountId_hook(OrbisNpOnlineId* online_id, u64* account_id) {
    LOG_DEBUG("called");
    if (online_id == nullptr || account_id == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        *account_id = 0;
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }
    *account_id = 0xFEEDFACE;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetAccountIdA_hook(OrbisUserServiceUserId user_id, u64* account_id) {
    LOG_DEBUG("user_id {}", user_id);
    if (account_id == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        *account_id = 0;
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }
    *account_id = 0xFEEDFACE;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetNpId_hook(OrbisUserServiceUserId user_id, OrbisNpId* np_id) {
    LOG_DEBUG("user_id {}", user_id);
    if (np_id == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }
    memset(np_id, 0, sizeof(OrbisNpId));
    strncpy(np_id->handle.data, Config::getUserName().c_str(), sizeof(np_id->handle.data));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetOnlineId_hook(OrbisUserServiceUserId user_id, OrbisNpOnlineId* online_id) {
    LOG_DEBUG("user_id {}", user_id);
    if (online_id == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }
    memset(online_id, 0, sizeof(OrbisNpOnlineId));
    strncpy(online_id->data, Config::getUserName().c_str(), sizeof(online_id->data));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetNpReachabilityState_hook(OrbisUserServiceUserId user_id,
                                                  OrbisNpReachabilityState* state) {
    if (state == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    *state =
        g_signed_in ? OrbisNpReachabilityState::Reachable : OrbisNpReachabilityState::Unavailable;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpGetState_hook(OrbisUserServiceUserId user_id, OrbisNpState* state) {
    if (state == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    *state = g_signed_in ? OrbisNpState::SignedIn : OrbisNpState::SignedOut;
    LOG_DEBUG("Signed {}", g_signed_in ? "in" : "out");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpHasSignedUp_hook(OrbisUserServiceUserId user_id, bool* has_signed_up) {
    LOG_DEBUG("called");
    if (has_signed_up == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    *has_signed_up = g_signed_in ? true : false;
    return ORBIS_OK;
}

struct NpStateCallbackForNpToolkit {
    OrbisNpStateCallbackForNpToolkit func;
    void* userdata;
};

NpStateCallbackForNpToolkit NpStateCbForNp;

s32 PS4_SYSV_ABI sceNpCheckCallback_hook() {
    LOG_DEBUG("(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpCheckCallbackForLib_hook() {
    LOG_DEBUG("(STUBBED) called");
    return ORBIS_OK;
}

HOOK_INIT(sceHttpCreateRequestWithURL);
s32 sceHttpCreateRequestWithURL_hook(s32 tmpl_id, s32 method, const char* url, u64 content_length) {
    std::string new_url = ReplaceHost(std::string(url));
    LOG_INFO("Replaced {} with {} 1", url, new_url);
    return HOOK_CONTINUE(sceHttpCreateRequestWithURL, s32 (*)(s32, s32, const char*, u64), tmpl_id,
                         method, new_url.c_str(), content_length);
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
    HOOK(sceHttpCreateRequestWithURL);

    HOOK(sceNpCreateRequest);
    HOOK(sceNpCreateAsyncRequest);
    HOOK(sceNpCheckNpAvailability);
    HOOK(sceNpCheckNpAvailabilityA);
    HOOK(sceNpCheckNpReachability);
    HOOK(sceNpCheckPlus);
    HOOK(sceNpGetAccountLanguage);
    HOOK(sceNpGetAccountLanguageA);
    HOOK(sceNpGetParentalControlInfo);
    HOOK(sceNpGetParentalControlInfoA);
    HOOK(sceNpAbortRequest);
    HOOK(sceNpWaitAsync);
    HOOK(sceNpPollAsync);
    HOOK(sceNpDeleteRequest);
    HOOK(sceNpGetAccountCountry);
    HOOK(sceNpGetAccountCountryA);
    HOOK(sceNpGetAccountDateOfBirth);
    HOOK(sceNpGetAccountDateOfBirthA);
    HOOK(sceNpGetGamePresenceStatus);
    HOOK(sceNpGetGamePresenceStatusA);
    HOOK(sceNpGetAccountId);
    HOOK(sceNpGetAccountIdA);
    HOOK(sceNpGetNpId);
    HOOK(sceNpGetOnlineId);
    HOOK(sceNpGetNpReachabilityState);
    HOOK(sceNpGetState);
    HOOK(sceNpHasSignedUp);
    HOOK(sceNpCheckCallback);
    HOOK(sceNpCheckCallbackForLib);
    return 0;
}

s32 attr_public plugin_unload(s32 argc, const char* argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    UNHOOK(sceHttpCreateRequestWithURL);

    UNHOOK(sceNpCreateRequest);
    UNHOOK(sceNpCreateAsyncRequest);
    UNHOOK(sceNpCheckNpAvailability);
    UNHOOK(sceNpCheckNpAvailabilityA);
    UNHOOK(sceNpCheckNpReachability);
    UNHOOK(sceNpCheckPlus);
    UNHOOK(sceNpGetAccountLanguage);
    UNHOOK(sceNpGetAccountLanguageA);
    UNHOOK(sceNpGetParentalControlInfo);
    UNHOOK(sceNpGetParentalControlInfoA);
    UNHOOK(sceNpAbortRequest);
    UNHOOK(sceNpWaitAsync);
    UNHOOK(sceNpPollAsync);
    UNHOOK(sceNpDeleteRequest);
    UNHOOK(sceNpGetAccountCountry);
    UNHOOK(sceNpGetAccountCountryA);
    UNHOOK(sceNpGetAccountDateOfBirth);
    UNHOOK(sceNpGetAccountDateOfBirthA);
    UNHOOK(sceNpGetGamePresenceStatus);
    UNHOOK(sceNpGetGamePresenceStatusA);
    UNHOOK(sceNpGetAccountId);
    UNHOOK(sceNpGetAccountIdA);
    UNHOOK(sceNpGetNpId);
    UNHOOK(sceNpGetOnlineId);
    UNHOOK(sceNpGetNpReachabilityState);
    UNHOOK(sceNpGetState);
    UNHOOK(sceNpHasSignedUp);
    UNHOOK(sceNpCheckCallback);
    UNHOOK(sceNpCheckCallbackForLib);
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void* args) {
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void* args) {
    return 0;
}

} // extern "C"