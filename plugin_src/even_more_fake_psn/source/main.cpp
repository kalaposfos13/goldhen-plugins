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
#include "np_types.h"
#include "np_web_api.h"
#include "np_score.h"

#include "ssl.h"

#include "np_signaling.h"

using namespace Libraries::Np::NpManager;
// using namespace Libraries::Np;
// using namespace Libraries::Np::NpAuth;
// using namespace Libraries::Np::NpWebApi;

namespace Config {
std::string getUserName() {
    int u;
    sceUserServiceGetInitialUser(&u);
    char n[32];
    sceUserServiceGetUserName(u, n, sizeof(n));
    return std::string(n);
}
} // namespace Config


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

static s32 g_active_auth_requests = 0;
static std::mutex g_auth_request_mutex;

const char* g_dummy_auth_code = "DUMMY-CODE";

enum class NpAuthRequestState {
    None = 0,
    Ready = 1,
    Aborted = 2,
    Complete = 3,
};

struct NpAuthRequest {
    NpAuthRequestState state;
    bool async;
    s32 result;
};

static std::vector<NpAuthRequest> g_auth_requests;

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



const char *RedirectURL = "http://bbnet.yahargul.info:20443";

extern "C" {

attr_public const char* g_pluginName = "even faker psn";
attr_public const char* g_pluginDesc = "";
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


HOOK_INIT(sceNpAuthGetAuthorizationCode);
HOOK_INIT(sceNpAuthGetAuthorizationCodeA);
// HOOK_INIT(sceNpAuthGetAuthorizationCodeV3);
HOOK_INIT(sceNpAuthCreateAsyncRequest);
HOOK_INIT(sceNpAuthCreateRequest);
HOOK_INIT(sceNpAuthDeleteRequest);
HOOK_INIT(sceNpAuthPollAsync);

HOOK_INIT(sceNpManagerIntGetSigninState);
HOOK_INIT(sceNpManagerIntIsSubAccount);

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
HOOK_INIT(sceHttpCreateConnectionWithURL);
HOOK_INIT(sceHttpCreateRequestWithURL);

HOOK_INIT(sceSslInit);
HOOK_INIT(sceNpSignalingInitialize);
HOOK_INIT(sceNpScoreCreateNpTitleCtx);



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

s32 GetAuthorizationCode(s32 req_id, const OrbisNpAuthGetAuthorizationCodeParameterA* param,
                         s32 flag, OrbisNpAuthorizationCode* auth_code, s32* issuer_id) {

    LOG_ERROR("GETAUTHLOOP CALLED");
    if (param == nullptr || auth_code == nullptr) {
        LOG_ERROR("PARAM OR AUTH WAS NULL");
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }
    if (param->size != sizeof(OrbisNpAuthGetAuthorizationCodeParameter)) {
        LOG_ERROR("PARAM SIZE MISMATCH");
        return ORBIS_NP_AUTH_ERROR_INVALID_SIZE;
    }
    if (param->user_id == -1 || param->client_id == nullptr || param->scope == nullptr) {
        LOG_ERROR("USER ID, CLIENT ID, OR SCOPE ARE INCORRECT OR NULL");
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    }

    std::scoped_lock lk{g_auth_request_mutex};

    // From here the actual authorization code request is performed.
    s32 req_index = req_id - ORBIS_NP_AUTH_REQUEST_ID_OFFSET - 1;
    if (g_active_auth_requests == 0 || g_auth_requests.size() <= req_index ||
        g_auth_requests[req_index].state == NpAuthRequestState::None) {
        LOG_ERROR("NO REQUEST FOUND");
        return ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND;
    }

    auto& request = g_auth_requests[req_index];
    if (request.state == NpAuthRequestState::Complete) {
        request.result = ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
        LOG_ERROR("INVALID AUTH ARGUMENT");
        return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    } else if (request.state == NpAuthRequestState::Aborted) {
        request.result = ORBIS_NP_AUTH_ERROR_ABORTED;
        LOG_ERROR("REQUEST ABORTED");
        return ORBIS_NP_AUTH_ERROR_ABORTED;
    }

    request.state = NpAuthRequestState::Complete;
    if (!g_signed_in) {
        request.result = ORBIS_NP_ERROR_SIGNED_OUT;
        LOG_ERROR("NOT SIGNED IN");
        // If the request is processed in some form, and it's an async request, then it returns OK.
        if (request.async) {
            return ORBIS_OK;
        }
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }

    LOG_ERROR("(STUBBED) called, req_id = '{}', async = '{}'", req_id, request.async);

    std::memcpy(auth_code, g_dummy_auth_code, sizeof(OrbisNpAuthorizationCode));
    // Not sure about the fifth argument
    *issuer_id = std::strlen(g_dummy_auth_code);

    LOG_ERROR("GetAuthCode SUCCESS: Returning Token: '{}', IssuerID: '{}'", g_dummy_auth_code,
              *issuer_id);

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


const char *extract_path(const char *url) {
    if (!url) return NULL;
    const char *start = strstr(url, "://");
    if (!start) return NULL;
    start += 3;
    const char *path = strchr(start, '/');
    return path ? path : "";
}

char *GetUrltoRedirect(const char *url) {
    if (!url) return NULL;

    if (strstr(url, "ss4.scej-network.jp") != NULL || 
        strstr(url, "bb.scej-network.jp") != NULL )
    {
        const char *Path = extract_path(url);
        if (!Path) return NULL;

        size_t newUrlSize = strlen(RedirectURL) + strlen(Path) + 1;
        char *newRedirectUrl = (char *)malloc(newUrlSize);
        if (!newRedirectUrl) return NULL;

        strcpy(newRedirectUrl, RedirectURL);
        strcat(newRedirectUrl, Path);
        return newRedirectUrl; 
    }

    return NULL;
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

LOG_ERROR("NPSETCONTENTRESTRICTION called, returning zero to '{}'", __builtin_return_address(0));

return 0;
}


int32_t sceHttpCreateRequestWithURL_hook(int32_t conectId, int32_t method, const char *url, uint64_t contentLength) {
    //This will likely be temporary, the change to eboot most likely will make this unnecessary, but it is here for logging purposes.
    
    char * newRedirectUrl = GetUrltoRedirect(url);
    LOG_ERROR("sceHttp::sceHttpCreateRequestWithURL->Url: '{}'", url);

    if (newRedirectUrl) {       
        LOG_ERROR("Redirecting CreateRequest URL:");
        LOG_ERROR("Original: '{}'", url);
        LOG_ERROR("Redirected: '{}'", newRedirectUrl);
        
        int32_t result = HOOK_CONTINUE(sceHttpCreateRequestWithURL, 
            int32_t(*)(int32_t, int32_t, const char *, uint64_t), conectId, method, newRedirectUrl, contentLength);
        
        free(newRedirectUrl);
        return result;
    } 
    
    return HOOK_CONTINUE(sceHttpCreateRequestWithURL, 
        int32_t(*)(int32_t, int32_t, const char *, uint64_t), 
        conectId, method, url, contentLength);
}

int32_t sceHttpCreateConnectionWithURL_hook(int32_t templateId, const char *url, bool isKeepalive) {
    char * newRedirectUrl = GetUrltoRedirect(url);
    // This will likely be temporary, the change to eboot most likely will make this unnecessary,
    // but it is here for logging purposes.
    if (newRedirectUrl) {
        
        LOG_ERROR("Redirecting CreateConnection URL:");
        LOG_ERROR("Original: '{}'", url);
        LOG_ERROR("Redirected: '{}'", newRedirectUrl);
        
        int32_t result = HOOK_CONTINUE(sceHttpCreateConnectionWithURL, 
            int32_t(*)(int32_t, const char *, bool), 
            templateId, newRedirectUrl, isKeepalive);
        
        free(newRedirectUrl);
        return result;
    }

    return HOOK_CONTINUE(sceHttpCreateConnectionWithURL, 
        int32_t(*)(int32_t, const char *, bool), 
        templateId, url, isKeepalive);
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
    LOG_ERROR("ASYNC AUTH POLL CALLED");
	
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
    LOG_ERROR("called req_id = '{}', returning result = '{}'", req_id, static_cast<u32>(*result));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpAuthCreateAsyncRequest_hook(const OrbisNpAuthCreateAsyncRequestParameter* param) {
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

s32 PS4_SYSV_ABI sceNpManagerIntGetSigninState_hook(int retSignin) {
    LOG_ERROR("INT GET SIGNIN STATE WAS CALLED! RETURNING OK");
    retSignin = 1;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpManagerIntIsSubAccount_hook(bool SubAccount) {
    LOG_ERROR("INT IS SUB ACCOUNT WAS CALLED! RETURNING OK");
    SubAccount = true;

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

    return GetAuthorizationCode(req_id, &internal_params, 0, auth_code, issuer_id);
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

    return GetAuthorizationCode(req_id, param, 0, auth_code, issuer_id);
}

s32 PS4_SYSV_ABI sceNpAuthGetAuthorizationCodeV3_hook(
    s32 req_id, const OrbisNpAuthGetAuthorizationCodeParameterA* param,
    OrbisNpAuthorizationCode* auth_code, s32* issuer_id) {
    return GetAuthorizationCode(req_id, param, 1, auth_code, issuer_id);
}

s32 PS4_SYSV_ABI sceNpCreateRequest_hook() {
    LOG_DEBUG("called");
    return CreateNpRequest(false);
}

s32 PS4_SYSV_ABI sceNpCreateAsyncRequest_hook(const OrbisNpCreateAsyncRequestParameter* param) {
    LOG_ERROR("SCENPCREATEASYNCREQUEST CALLED!  AUTH IS COMING!");
    if (param == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }

    if (param->size != sizeof(OrbisNpCreateAsyncRequestParameter)) {
        return ORBIS_NP_ERROR_INVALID_SIZE;
    }
    LOG_ERROR("SCENPCREATEASYNCREQUEST WAS SUCCESSFUL! CREATING NPREQUEST, SETTING ASYNC TO TRUE!");
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
    LOG_ERROR("user_id {}", user_id);

    static const char* kTestName = "metrikPS4";
    if (online_id == nullptr) {
        return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    }
    if (!g_signed_in) {
        return ORBIS_NP_ERROR_SIGNED_OUT;
    }
    memset(online_id, 0, sizeof(OrbisNpOnlineId));
    LOG_ERROR("Online_id = '{}'", (void*)online_id);
    strncpy(online_id->data, kTestName, sizeof(online_id->data));
    LOG_ERROR("Online_id = '{}'", online_id->data);
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

//HOOK_INIT(sceHttpCreateRequestWithURL);
//s32 sceHttpCreateRequestWithURL_hook(s32 tmpl_id, s32 method, const char* url, u64 content_length) {
//    std::string new_url = ReplaceHost(std::string(url));
//    LOG_INFO("Replaced {} with {} 1", url, new_url);
//    return HOOK_CONTINUE(sceHttpCreateRequestWithURL, s32 (*)(s32, s32, const char*, u64), tmpl_id,
//                         method, new_url.c_str(), content_length);
//}

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
    HOOK(sceNpAuthGetAuthorizationCode);
    HOOK(sceNpAuthGetAuthorizationCodeA);
    // HOOK(sceNpAuthGetAuthorizationCodeV3);
    HOOK(sceNpManagerIntGetSigninState);
    HOOK(sceNpManagerIntIsSubAccount);
    HOOK(sceNpAuthCreateRequest);
    HOOK(sceNpAuthCreateAsyncRequest);
    HOOK(sceNpAuthDeleteRequest);
    HOOK(sceNpAuthPollAsync);
    HOOK(sceNpWebApiCreateRequest);
    HOOK(sceNpWebApiSendRequest);
    HOOK(sceNpWebApiGetHttpStatusCode);
    HOOK(sceNpWebApiReadData);
    HOOK(sceNpWebApiDeleteRequest);
    HOOK(sceHttpsEnableOption);
    HOOK(sceHttpsDisableOption);
	HOOK(sceHttpCreateConnectionWithURL);
	HOOK(sceHttpCreateRequestWithURL);
    HOOK(sceSslInit);
    HOOK(sceNpSignalingInitialize);
    HOOK(sceNpScoreCreateNpTitleCtx);
    HOOK(sceNpWebApiCreateContext);
    HOOK(sceNpWebApiCreatePushEventFilter);
    HOOK(sceNpWebApiRegisterPushEventCallback);      
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
    UNHOOK(sceNpAuthGetAuthorizationCode);
    UNHOOK(sceNpAuthGetAuthorizationCodeA);
    // UNHOOK(sceNpAuthGetAuthorizationCodeV3);
    UNHOOK(sceNpAuthCreateAsyncRequest);
    UNHOOK(sceNpAuthCreateRequest);
    UNHOOK(sceNpAuthDeleteRequest);
    UNHOOK(sceNpAuthPollAsync);
    UNHOOK(sceNpManagerIntGetSigninState);
    UNHOOK(sceNpManagerIntIsSubAccount);
    UNHOOK(sceNpWebApiCreateRequest);
    UNHOOK(sceNpWebApiSendRequest);
    UNHOOK(sceNpWebApiGetHttpStatusCode);
    UNHOOK(sceNpWebApiReadData);
    UNHOOK(sceNpWebApiDeleteRequest);
    UNHOOK(sceHttpsEnableOption);
    UNHOOK(sceHttpsDisableOption);
	UNHOOK(sceHttpCreateConnectionWithURL);
	UNHOOK(sceHttpCreateRequestWithURL);
    UNHOOK(sceSslInit);
    UNHOOK(sceNpSignalingInitialize);
    UNHOOK(sceNpScoreCreateNpTitleCtx);
    UNHOOK(sceNpWebApiCreateContext);
    UNHOOK(sceNpWebApiCreatePushEventFilter);
    UNHOOK(sceNpWebApiRegisterPushEventCallback);        
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void* args) {
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void* args) {
    return 0;
}

} // extern "C"