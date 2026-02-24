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

#include <orbis/Http.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include "np_auth.h"
#include "np_auth_error.h"
#include "np_error.h"
#include "np_manager.h"
#include "np_matching.h"
#include "np_score.h"
#include "np_types.h"
#include "np_web_api.h"
#include "np_signaling.h"
#include "ssl.h"

using namespace Libraries::Np::NpManager;



static bool g_signed_in = true;
static s32 g_active_requests = 0;
static std::mutex g_request_mutex;

static s32 g_active_auth_requests = 0;
static std::mutex g_auth_request_mutex;

enum class OrbisUserServiceEventType {
    Login = 0,  // Login event
    Logout = 1, // Logout event
};
struct OrbisUserServiceEvent {
    OrbisUserServiceEventType event;
    OrbisUserServiceUserId userId;
};


// Internal types for storing request-related information
enum class NpRequestState {
    None = 0,
    Ready = 1,
    Aborted = 2,
    Complete = 3,
};
enum class NpAuthRequestState {
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
struct NpAuthRequest {
    NpAuthRequestState state;
    bool async;
    s32 result;
};
static std::vector<NpRequest> g_requests;
static std::vector<NpAuthRequest> g_auth_requests;

// Generic WebApi Mock responses
static std::map<SceNpWebApiMockRequestType, std::string>& get_templates() {
    static std::map<SceNpWebApiMockRequestType, std::string> instance{
        {REQ_BLOCK_LIST, "{\"totalResults\": 0, \"blockList\": []}"},
        {REQ_FRIEND_LIST, "{\"totalResults\": 0, \"friendList\": []}"},
    };
    return instance;
}
static std::map<s64, SceNpWebApiMockRequestType>& get_mrequests() {
    static std::map<s64, SceNpWebApiMockRequestType> instance;
    return instance;
}
static std::mutex& get_mrequests_mutex() {
    static std::mutex instance;
    return instance;
}

extern "C" {

attr_public const char* g_pluginName = "SceLibLogging";
attr_public const char* g_pluginDesc = "A R&D framework plugin to better understand game interactions with various console libraries.  This will likely crash any game using it eventually if not outright, but will provide some level of logging to aid in troubleshooting efforts.";
attr_public const char* g_pluginAuth = "kalaposfos, metr1k";
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
HOOK_INIT(sceNpSetContentRestriction);
HOOK_INIT(sceNpRegisterStateCallback);
HOOK_INIT(sceNpNotifyPlusFeature);
HOOK_INIT(sceNpRegisterPlusEventCallback);
HOOK_INIT(sceNpSetNpTitleId);
HOOK_INIT(sceNpUnregisterPlusEventCallback);
HOOK_INIT(sceNpUnregisterStateCallback);
HOOK_INIT(sceNpRegisterGamePresenceCallback);

HOOK_INIT(sceNpAuthGetAuthorizationCode);
HOOK_INIT(sceNpAuthGetAuthorizationCodeA);
HOOK_INIT(sceNpAuthCreateAsyncRequest);
HOOK_INIT(sceNpAuthCreateRequest);
HOOK_INIT(sceNpAuthDeleteRequest);
HOOK_INIT(sceNpAuthPollAsync);

HOOK_INIT(sceNpWebApiCreateRequest);
HOOK_INIT(sceNpWebApiSendRequest);
HOOK_INIT(sceNpWebApiGetHttpStatusCode);
HOOK_INIT(sceNpWebApiReadData);
HOOK_INIT(sceNpWebApiDeleteRequest);
HOOK_INIT(sceNpWebApiCreateContext);
HOOK_INIT(sceNpWebApiCreatePushEventFilter);
HOOK_INIT(sceNpWebApiRegisterPushEventCallback);

HOOK_INIT(sceHttpsDisableOption);
HOOK_INIT(sceHttpsEnableOption);

HOOK_INIT(sceSslInit);

HOOK_INIT(sceNpSignalingInitialize);
HOOK_INIT(sceNpSignalingCreateContext);
HOOK_INIT(sceNpSignalingActivateConnection);
HOOK_INIT(sceNpSignalingDeactivateConnection);
HOOK_INIT(sceNpSignalingDeleteContext);
HOOK_INIT(sceNpSignalingGetConnectionStatus);
HOOK_INIT(sceNpSignalingTerminate);

HOOK_INIT(sceNpScoreCreateNpTitleCtx);

HOOK_INIT(sceNpMatching2RegisterContextCallback);
HOOK_INIT(sceNpMatching2CreateContext);
HOOK_INIT(sceNpMatching2ContextStart);
HOOK_INIT(sceNpMatching2Initialize);
HOOK_INIT(sceNpMatching2GetServerId);
HOOK_INIT(sceNpMatching2SetDefaultRequestOptParam);
HOOK_INIT(sceNpMatching2RegisterRoomEventCallback);
HOOK_INIT(sceNpMatching2RegisterSignalingCallback);
HOOK_INIT(sceNpMatching2RegisterLobbyEventCallback);
HOOK_INIT(sceNpMatching2GetWorldInfoList);

HOOK_INIT(sceUserServiceGetUserName);
HOOK_INIT(sceUserServiceGetInitialUser);
HOOK_INIT(sceUserServiceGetLoginUserIdList);
HOOK_INIT(sceUserServiceInitialize);
HOOK_INIT(sceUserServiceGetEvent);


s32 PS4_SYSV_ABI sceUserServiceInitialize_hook(const OrbisUserServiceInitializeParams* initParams) {
    LOG_WARNING("(dummy) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceUserServiceGetLoginUserIdList_hook(OrbisUserServiceLoginUserIdList* userIdList) {
    LOG_DEBUG("called");
    if (userIdList == nullptr) {
        LOG_ERROR("user_id is null");
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;
    }
    // TODO only first user, do the others as well
    userIdList->userId[0] = 1;
    userIdList->userId[1] = ORBIS_USER_SERVICE_ERROR_NOT_LOGGED_IN;
    userIdList->userId[2] = ORBIS_USER_SERVICE_ERROR_NOT_LOGGED_IN;
    userIdList->userId[3] = ORBIS_USER_SERVICE_ERROR_NOT_LOGGED_IN;

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceUserServiceGetInitialUser_hook(int* user_id) {
    LOG_DEBUG("called");
    if (user_id == nullptr) {
        LOG_ERROR("user_id is null");
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;
    }
    // select first user (TODO add more)
    *user_id = 1;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceUserServiceGetEvent_hook(OrbisUserServiceEvent* event) {
    LOG_ERROR("(DUMMY) called");
    // fake a loggin event
    static bool logged_in = false;

    if (!logged_in) {
        logged_in = true;
        event->event = OrbisUserServiceEventType::Login;
        event->userId = 1;
        return ORBIS_OK;
    }

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceUserServiceGetUserName_hook(int user_id, char* user_name, std::size_t size) {
    LOG_ERROR("sceUserServiceGetUserName_hook: user_id {}, size {}", user_id, size);

    if (!user_name)
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;

    const char* kTestName = "testname";
    size_t len = strlen(kTestName);

    // Must allow space for null terminator
    if (size <= len)
        return ORBIS_USER_SERVICE_ERROR_BUFFER_TOO_SHORT;

    memcpy(user_name, kTestName, len + 1);

    LOG_ERROR("sceUserServiceGetUserName_hook returning '{}'", user_name);

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpSignalingActivateConnection_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpSignalingCreateContext_hook(s32 param_1, void* param_2, void* param_3,
                                             s32* context_id) {
    LOG_ERROR("(STUBBED) sceNpSignalingCreateContext called");
    static s32 context_id_counter = 0;
    *context_id = ++context_id_counter;

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpSignalingDeactivateConnection_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpSignalingDeleteContext_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpSignalingGetConnectionStatus_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpSignalingTerminate_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpSetNpTitleId_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2Initialize_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2RegisterContextCallback_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2GetWorldInfoList_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2CreateContext_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2ContextStart_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2GetServerId_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2SetDefaultRequestOptParam_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2RegisterRoomEventCallback_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2RegisterSignalingCallback_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpMatching2RegisterLobbyEventCallback_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpRegisterGamePresenceCallback_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpRegisterStateCallback_hook() {
    LOG_ERROR("(STUBBED) called,returning zero to {}", __builtin_return_address(0));
    return 0;
}

s32 PS4_SYSV_ABI sceNpWebApiCreateContext_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpWebApiCreatePushEventFilter_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpWebApiRegisterPushEventCallback_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

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

s32 CreateNpAuthRequest(bool async) {
    LOG_ERROR("CREATE NPAUTH REQUEST MADE!");

    if (g_active_auth_requests == ORBIS_NP_AUTH_REQUEST_LIMIT) {
        return ORBIS_NP_AUTH_ERROR_REQUEST_MAX;
    }
    LOG_ERROR("NOT AT NPAUTH LIMIT!");
    std::scoped_lock lk{g_auth_request_mutex};
    LOG_ERROR("NPAUTH ADDING TO INDEX!");
    s32 req_index = 0;
    while (req_index < g_auth_requests.size()) {
        // Find first nonexistant request
        if (g_auth_requests[req_index].state == NpAuthRequestState::None) {
            // There is no request at this index, set the index to ready then break.
            g_auth_requests[req_index].state = NpAuthRequestState::Ready;
            g_auth_requests[req_index].async = async;
            break;
        }
        req_index++;
    }

    if (req_index == g_auth_requests.size()) {
        // There are no requests to replace.
        NpAuthRequest new_request{NpAuthRequestState::Ready, async, 0};
        g_auth_requests.emplace_back(new_request);
        LOG_ERROR("REQUEST ADDED NPAUTH");
    }

    // Offset by one, first returned ID is 0x10000001
    g_active_auth_requests++;
    LOG_ERROR("called, async = {}", async);
    return req_index + ORBIS_NP_AUTH_REQUEST_ID_OFFSET + 1;
}

int PS4_SYSV_ABI sceNpScoreCreateNpTitleCtx_hook() {
    LOG_ERROR("(STUBBED) called");

    static s32 title_ctx_id_counter = 0;
    s32 title_ctx_id = title_ctx_id_counter++;

    return title_ctx_id;
}

s32 PS4_SYSV_ABI sceNpAuthCreateRequest_hook() {
    LOG_ERROR("NPAUTH CREATE REQUEST called");
    return CreateNpAuthRequest(false);
}

s32 PS4_SYSV_ABI sceNpSignalingInitialize_hook() {
    LOG_ERROR("(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceSslInit_hook(std::size_t poolSize) {
    LOG_ERROR("(DUMMY) called poolSize = {}", poolSize);
    // return a value >1
    static int id = 0;
    return ++id;
}

int32_t sceNpSetContentRestriction_hook() {

    LOG_ERROR("NPSETCONTENTRESTRICTION called, returning zero to '{}'",
              __builtin_return_address(0));

    return 0;
}

int PS4_SYSV_ABI sceHttpsEnableOption_hook(u32 options) {
    LOG_ERROR("HTTPS Enable called, returning zero to {}", __builtin_return_address(0));
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceHttpsDisableOption_hook() {
    LOG_ERROR("HTTPS Disable called, returning zero to '{}'", __builtin_return_address(0));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpAuthPollAsync_hook(s32 req_id, s32* result) {
    // LOG_ERROR("ASYNC AUTH POLL CALLED");

    if (result == nullptr) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_auth_request_mutex};

    s32 req_index = req_id - ORBIS_NP_AUTH_REQUEST_ID_OFFSET - 1;
    if (g_active_auth_requests == 0 || g_auth_requests.size() <= req_index ||
        g_auth_requests[req_index].state == NpAuthRequestState::None) {
        return ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND;
    }

    if (!g_auth_requests[req_index].async ||
        g_auth_requests[req_index].state == NpAuthRequestState::Ready) {
        return ORBIS_NP_AUTH_ERROR_INVALID_ID;
    }

    // Since we're not actually performing any sort of network request here,
    // we can just set result based on the request and return.
    *result = g_auth_requests[req_index].result;
    // LOG_ERROR("called req_id = '{}', returning result = '{}'", req_id,
    // static_cast<u32>(*result));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI
sceNpAuthCreateAsyncRequest_hook(const OrbisNpAuthCreateAsyncRequestParameter* param) {
    LOG_ERROR("AUTH CREATE ASYNC REQUEST MADE!");
    if (param == nullptr) {
        LOG_ERROR("PARAMETER WAS NULL");
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthCreateAsyncRequestParameter)) {
        LOG_ERROR("PARAMETER SIZE MISMATCH");
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }

    return CreateNpAuthRequest(true);
}

s32 PS4_SYSV_ABI sceNpAuthDeleteRequest_hook(s32 req_id) {
    LOG_ERROR("called req_id = '{}'", req_id);

    std::scoped_lock lk{g_auth_request_mutex};

    s32 req_index = req_id - ORBIS_NP_AUTH_REQUEST_ID_OFFSET - 1;
    if (g_active_auth_requests == 0 || g_auth_requests.size() <= req_index ||
        g_auth_requests[req_index].state == NpAuthRequestState::None) {
        return ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND;
    }

    g_active_auth_requests--;
    g_auth_requests[req_index].state = NpAuthRequestState::None;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpWebApiSendRequest_hook(s32 title_user_ctx_id, s64 request_id) {
    LOG_ERROR("(STUBBED) SendRequest called for ID: '{}'", request_id);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpWebApiGetHttpStatusCode_hook(s64 request_id, s32* out_status_code) {
    LOG_ERROR("(STUBBED) called, request_id: '{}'", request_id);

    if (out_status_code == nullptr) {
        return ORBIS_NP_WEB_API_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(get_mrequests_mutex());
    auto& requests = get_mrequests();

    if (requests.find(request_id) != requests.end()) {
        *out_status_code = 200;
        LOG_ERROR("GetHttpStatusCode: Found ID '{}', returning 200 OK", request_id);
    } else {
        LOG_ERROR("GetHttpStatusCode: ID '{}' NOT FOUND returning 200 OK", request_id);
    }

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpWebApiReadData_hook(s64 request_id, char* data, u64 size) {
    LOG_ERROR("(STUBBED) called, request_id: '{}'", request_id);

    if (data == nullptr || size == 0) {
        return ORBIS_OK;
    }

    std::lock_guard<std::mutex> lock(get_mrequests_mutex());
    auto& requests = get_mrequests();
    auto& templates = get_templates();

    auto it = requests.find(request_id);
    if (it != requests.end()) {
        auto template_it = templates.find(it->second);
        if (template_it != templates.end()) {
            const std::string& response = template_it->second;

            u64 to_copy = (size < (u64)response.size()) ? size : (u64)response.size();

            memcpy(data, response.data(), to_copy);

            LOG_ERROR("ReadData: ID '{}' Type '{}' copying '{}' bytes to app.", request_id,
                      (int)it->second, to_copy);
            LOG_ERROR("JSON Body: {}", response);

            return static_cast<s32>(to_copy);
        }
    }

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpWebApiDeleteRequest_hook(s64 request_id) {
    LOG_ERROR("(STUBBED) called, request_id: '{}'", request_id);

    std::lock_guard<std::mutex> lock(get_mrequests_mutex());
    auto& requests = get_mrequests();

    auto it = requests.find(request_id);
    if (it != requests.end()) {
        requests.erase(it);
        LOG_ERROR("Deleted request ID '{}' from map", request_id);
    }

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpWebApiCreateRequest_hook(s32 title_user_ctx_id, const char* p_api_group,
                                               const char* p_path, s32 method,
                                               SceNpWebApiContentParameter* p_content_parameter,
                                               s64* p_request_id) {
    LOG_ERROR("WEB API CALLED!");

    if (p_path == nullptr) {
        return ORBIS_NP_WEB_API_INVALID_ARGUMENT;
    }

    if (p_content_parameter != nullptr) {
        const char* c_type =
            p_content_parameter->p_content_type ? p_content_parameter->p_content_type : "UNKNOWN";
        uint64_t c_len = p_content_parameter->content_length;

        LOG_ERROR("WEB API CONTENT: Type: %s, Length: %llu", c_type, (unsigned long long)c_len);
    } else {
        LOG_ERROR("NO CONTENT PARAMETER PROVIDED");
    }

    LOG_ERROR("P PATH ISN'T NULL");

    static s64 request_id_counter = 0;
    s64 request_id = request_id_counter++;

    LOG_ERROR("REQUEST ID ASSIGNED");

    *p_request_id = request_id;

    SceNpWebApiMockRequestType type = REQ_INVALID;

    std::lock_guard<std::mutex> lock(get_mrequests_mutex());

    if (strstr(p_path, "blockList") != nullptr) {
        type = REQ_BLOCK_LIST;
    } else if (strstr(p_path, "friendList") != nullptr) {
        type = REQ_FRIEND_LIST;
    }

    if (type == REQ_INVALID) {

        LOG_ERROR("No mock for request path: '{}'", p_path);
        return ORBIS_OK;
    }
    LOG_ERROR("STARTING EMPLACE");

    get_mrequests().emplace(request_id, type);

    LOG_ERROR("EMPLACE DONE, RETURNING.");

    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpAuthGetAuthorizationCode_hook(
    s32 req_id, const OrbisNpAuthGetAuthorizationCodeParameter* param,
    OrbisNpAuthorizationCode* auth_code, s32* issuer_id) {

    LOG_ERROR("GetAuthCode: Called! req_id: '{}'", req_id);

    if (param == nullptr || auth_code == nullptr) {
        LOG_ERROR("GetAuthCode: Error - Null parameters provided.");
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }

    if (param->size != sizeof(OrbisNpAuthGetAuthorizationCodeParameter)) {
        LOG_ERROR("GetAuthCode: Error - Invalid struct size ({} != {})", param->size,
                  sizeof(OrbisNpAuthGetAuthorizationCodeParameter));
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }

    if (param->online_id == nullptr || param->client_id == nullptr || param->scope == nullptr) {
        LOG_ERROR("GetAuthCode: Error - Missing OnlineID, ClientID, or Scope.");
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }

    if (!g_signed_in) {
        LOG_WARNING("GetAuthCode: Blocked - User is NOT signed in.");
        return ORBIS_NP_ERROR_USER_NOT_FOUND;
    }

    s32 user_id = 0;
    if (sceUserServiceGetInitialUser(&user_id) != 0) {
        LOG_ERROR("GetAuthCode: Critical - Failed to get initial user ID.");
        return ORBIS_NP_ERROR_USER_NOT_FOUND;
    }

    LOG_INFO("GetAuthCode: Resolving for UserID: '{}', Scope: '{}'", user_id, param->scope);

    OrbisNpAuthGetAuthorizationCodeParameterA internal_params;
    std::memset(&internal_params, 0, sizeof(internal_params));
    internal_params.size = sizeof(internal_params);
    internal_params.client_id = param->client_id;
    internal_params.user_id = user_id;
    internal_params.scope = param->scope;

    return HOOK_CONTINUE(sceNpAuthGetAuthorizationCode,
                         s32 (*)(s32, const OrbisNpAuthGetAuthorizationCodeParameter*,
                                 OrbisNpAuthorizationCode*, s32*),
                         req_id, param, auth_code, issuer_id);
}

s32 PS4_SYSV_ABI sceNpAuthGetAuthorizationCodeA_hook(
    s32 req_id, const OrbisNpAuthGetAuthorizationCodeParameterA* param,
    OrbisNpAuthorizationCode* auth_code, s32* issuer_id) {
    if (param) {
        LOG_INFO("GetAuthCodeA: Called for UserID: '{}', Scope: '{}'", param->user_id,
                 param->scope ? param->scope : "NULL");
    } else {
        LOG_ERROR("GetAuthCodeA: Called with NULL param!");
    }

        return HOOK_CONTINUE(sceNpAuthGetAuthorizationCodeA,
                         s32 (*)(s32, const OrbisNpAuthGetAuthorizationCodeParameterA*,
                                 OrbisNpAuthorizationCode*, s32*),
                         req_id, param, auth_code, issuer_id);
}


s32 PS4_SYSV_ABI sceNpCreateRequest_hook() {
    LOG_DEBUG("called");
    return CreateNpRequest(false);
}

s32 PS4_SYSV_ABI sceNpCreateAsyncRequest_hook(const OrbisNpCreateAsyncRequestParameter* param) {
    // LOG_ERROR("SCENPCREATEASYNCREQUEST CALLED!  AUTH IS COMING!");
    if (param == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    if (param->size != sizeof(OrbisNpCreateAsyncRequestParameter)) {
        return ORBIS_NP_ERROR_INVALID_SIZE;
    }
    // LOG_ERROR("SCENPCREATEASYNCREQUEST WAS SUCCESSFUL! CREATING NPREQUEST, SETTING ASYNC TO
    // TRUE!");
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

    // LOG_ERROR("(STUBBED) called, req_id = {:#x}, is_async = {}, param.features = {:#x}", req_id,
    // request.async, param->features);

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
    // LOG_WARNING("called req_id = {:#x}, returning result = {:#x}", req_id,
    // static_cast<u32>(*result));
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
        LOG_ERROR("sceNpGetNpId_hook: user_id {}", user_id);

        if (!np_id)
            return ORBIS_NP_ERROR_INVALID_ARGUMENT;

        if (!g_signed_in)
            return ORBIS_NP_ERROR_SIGNED_OUT;

        memset(np_id, 0, sizeof(OrbisNpId));

        const char* kTestName = "testname";
        strncpy(np_id->handle.data, kTestName, sizeof(np_id->handle.data));

        
        LOG_ERROR("NpId struct @ {}", (void*)np_id);
        LOG_ERROR("  handle.data = '{}'", np_id->handle.data);
        LOG_ERROR("  handle.term = {}", np_id->handle.term);
        return ORBIS_OK;
   
}

s32 PS4_SYSV_ABI sceNpGetOnlineId_hook(OrbisUserServiceUserId user_id, OrbisNpOnlineId* online_id) {
        LOG_ERROR("sceNpGetOnlineId_hook: user_id {}", user_id);

        if (!online_id)
            return ORBIS_NP_ERROR_INVALID_ARGUMENT;

        if (!g_signed_in)
            return ORBIS_NP_ERROR_SIGNED_OUT;

        memset(online_id, 0, sizeof(OrbisNpOnlineId));

        const char* kTestName = "testname";
        size_t len = strnlen(kTestName, ORBIS_NP_ONLINEID_MAX_LENGTH - 1);

        memcpy(online_id->data, kTestName, len);
        online_id->term = (s8)len;

        
        LOG_ERROR("OnlineId struct @ {}", (void*)online_id);
        LOG_ERROR("  data      = '{}'", online_id->data);
        LOG_ERROR("  term      = {}", online_id->term);
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

s32 attr_public plugin_load(s32 argc, const char* argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    final_printf("[GoldHEN] Plugin Author(s): %s\n", g_pluginAuth);
    boot_ver();
    struct proc_info procInfo;
    if (!sys_sdk_proc_info(&procInfo)) {
        memcpy(titleid, procInfo.titleid, sizeof(titleid));
        print_proc_info();
    }

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
    HOOK(sceNpSetContentRestriction);
    HOOK(sceNpRegisterGamePresenceCallback);
    HOOK(sceNpSetNpTitleId);
    HOOK(sceNpRegisterStateCallback);

    HOOK(sceNpAuthGetAuthorizationCode);
    HOOK(sceNpAuthGetAuthorizationCodeA);
    HOOK(sceNpAuthCreateRequest);
    HOOK(sceNpAuthCreateAsyncRequest);
    HOOK(sceNpAuthDeleteRequest);
    HOOK(sceNpAuthPollAsync);

    HOOK(sceNpWebApiCreateRequest);
    HOOK(sceNpWebApiSendRequest);
    HOOK(sceNpWebApiGetHttpStatusCode);
    HOOK(sceNpWebApiReadData);
    HOOK(sceNpWebApiDeleteRequest);
    HOOK(sceNpWebApiCreateContext);
    HOOK(sceNpWebApiCreatePushEventFilter);
    HOOK(sceNpWebApiRegisterPushEventCallback);

    HOOK(sceHttpsEnableOption);
    HOOK(sceHttpsDisableOption);

    HOOK(sceSslInit);
        
    HOOK(sceNpScoreCreateNpTitleCtx); 

    HOOK(sceNpMatching2RegisterContextCallback);
    HOOK(sceNpMatching2CreateContext);
    HOOK(sceNpMatching2ContextStart);
    HOOK(sceNpMatching2Initialize);
    HOOK(sceNpMatching2GetServerId);
    HOOK(sceNpMatching2SetDefaultRequestOptParam);
    HOOK(sceNpMatching2RegisterRoomEventCallback);
    HOOK(sceNpMatching2RegisterSignalingCallback);
    HOOK(sceNpMatching2RegisterLobbyEventCallback);
    HOOK(sceNpMatching2GetWorldInfoList);
    
 
    
    HOOK(sceNpSignalingCreateContext);
    HOOK(sceNpSignalingActivateConnection);
    HOOK(sceNpSignalingDeactivateConnection);
    HOOK(sceNpSignalingDeleteContext);
    HOOK(sceNpSignalingGetConnectionStatus);
    HOOK(sceNpSignalingTerminate);
    HOOK(sceNpSignalingInitialize);

    //HOOK(sceUserServiceGetUserName);
    //HOOK(sceUserServiceGetInitialUser);
    //HOOK(sceUserServiceGetLoginUserIdList);
    //HOOK(sceUserServiceInitialize);
    //HOOK(sceUserServiceGetEvent);  
    return 0;
}

s32 attr_public plugin_unload(s32 argc, const char* argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);

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
    UNHOOK(sceNpSetContentRestriction);
    UNHOOK(sceNpRegisterGamePresenceCallback);
    UNHOOK(sceNpRegisterStateCallback);
    UNHOOK(sceNpSetNpTitleId);

    UNHOOK(sceNpAuthGetAuthorizationCode);
    UNHOOK(sceNpAuthGetAuthorizationCodeA);
    UNHOOK(sceNpAuthCreateAsyncRequest);
    UNHOOK(sceNpAuthCreateRequest);
    UNHOOK(sceNpAuthDeleteRequest);
    UNHOOK(sceNpAuthPollAsync);

    UNHOOK(sceNpWebApiCreateRequest);
    UNHOOK(sceNpWebApiSendRequest);
    UNHOOK(sceNpWebApiGetHttpStatusCode);
    UNHOOK(sceNpWebApiReadData);
    UNHOOK(sceNpWebApiDeleteRequest);
    UNHOOK(sceNpWebApiCreateContext);
    UNHOOK(sceNpWebApiCreatePushEventFilter);
    UNHOOK(sceNpWebApiRegisterPushEventCallback);

    UNHOOK(sceHttpsEnableOption);
    UNHOOK(sceHttpsDisableOption);

    UNHOOK(sceSslInit);
    
    UNHOOK(sceNpScoreCreateNpTitleCtx);

    
    UNHOOK(sceNpMatching2RegisterContextCallback);
    UNHOOK(sceNpMatching2CreateContext);
    UNHOOK(sceNpMatching2ContextStart);
    UNHOOK(sceNpMatching2Initialize);
    UNHOOK(sceNpMatching2GetWorldInfoList);
    UNHOOK(sceNpMatching2SetDefaultRequestOptParam);
    UNHOOK(sceNpMatching2RegisterRoomEventCallback);
    UNHOOK(sceNpMatching2RegisterSignalingCallback);
    UNHOOK(sceNpMatching2RegisterLobbyEventCallback);
    UNHOOK(sceNpMatching2GetServerId);

    
    UNHOOK(sceNpSignalingInitialize);
    UNHOOK(sceNpSignalingCreateContext);
    UNHOOK(sceNpSignalingActivateConnection);
    UNHOOK(sceNpSignalingDeactivateConnection);
    UNHOOK(sceNpSignalingDeleteContext);
    UNHOOK(sceNpSignalingGetConnectionStatus);
    UNHOOK(sceNpSignalingTerminate);
 

    //UNHOOK(sceUserServiceGetUserName);
    //UNHOOK(sceUserServiceGetInitialUser);
    //UNHOOK(sceUserServiceGetLoginUserIdList);
    //UNHOOK(sceUserServiceInitialize);
    //UNHOOK(sceUserServiceGetEvent);
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void* args) {
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void* args) {
    return 0;
}

} // extern "C"