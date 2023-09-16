#include <assert.h>
#include <errno.h>
#include <memory.h>
#include <stdlib.h>
#include <sys/timerfd.h>

#include <curl/curl.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/log.h>
#include <applibs/networking.h>

#include "eventloop_timer_utilities.h"
#include "log_utils.h"
#include "logstash.h"
#include "main.h"

/// File descriptor for the timerfd running for cURL.
static EventLoopTimer *curlTimer = NULL;
static EventLoop *eventLoop = NULL; // not owned

static char* logstashPassword = NULL;
static CURLM *multi_handle = 0;

static bool IsNetworkReady(void)
{
    bool isNetworkReady = false;
    if (Networking_IsNetworkingReady(&isNetworkReady) == -1) {
        Log_Debug("ERROR: Networking_IsNetworkingReady: %d (%s)\n", errno, strerror(errno));
        return false;
    }

    if (!isNetworkReady) {
        Log_Debug("WARNING: Not doing download because the network is not ready.\n");
    }
    return isNetworkReady;
}

static void LogCurlMultiError(const char *message, CURLMcode code)
{
    Log_Debug(message);
    Log_Debug(" (curl multi err=%d, '%s')\n", code, curl_multi_strerror(code));
}

static void CurlTimerEventHandler(EventLoopTimer* timer)
{
    if (ConsumeEventLoopTimerEvent(timer) != 0) {
        LogErrno("ERROR: cannot consume the timer event");
        return;
    }

    if (!IsNetworkReady()) {
        return;
    }

    // TODO: need retry logic of some kind
    
    int still_running;
    CURLMcode mc = curl_multi_perform(multi_handle, &still_running);
    if (mc) {
        LogCurlMultiError("ERROR: curl_multi_perform failed", mc);
        return;
    }

    CURLMsg* msg; /* for picking up messages with the transfer status */
    int msgs_left; /* how many messages are left */

    while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            Log_Debug("HTTP transfer completed with status %d\n", msg->data.result);

            curl_multi_remove_handle(multi_handle, msg->easy_handle);
            curl_easy_cleanup(msg->easy_handle);
        }
    }
}

static ExitCode CurlInit(void)
{
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        Log_Debug("curl_global_init failed!\n");
        return ExitCode_CurlInit_GlobalInit;
    }

    multi_handle = curl_multi_init();
    if (multi_handle == NULL) {
        Log_Debug("curl_multi_init() failed!\n");
        return ExitCode_CurlInit_MultiInit;
    }

    return ExitCode_Success;
}

static void CurlFini(void)
{
    // TODO: clean up transfers
    curl_global_cleanup();
}

void SendToLogstash(char* url, char* postBody)
{
    if (!IsNetworkReady()) {
        Log_Debug("Network is not ready, skipping send\n");
        return;
    }

    char authHeader[64] = {};
    snprintf(authHeader, sizeof(authHeader), "science_user:%s", logstashPassword);
    char logstashAuthHeader[64] = {};
    snprintf(logstashAuthHeader, sizeof(logstashAuthHeader), "SaintGimp-Private-Key: %s", logstashPassword);
    CURL* handle = curl_easy_init();

    struct curl_slist* hs = NULL;
    curl_easy_setopt(handle, CURLOPT_URL, url);
    // TODO: need to install the public CA cert for LetsEncrypt?
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, false);
    curl_easy_setopt(handle, CURLOPT_COPYPOSTFIELDS, postBody);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(handle, CURLOPT_USERPWD, authHeader);
    hs = curl_slist_append(hs, "Content-Type: application/json");
    hs = curl_slist_append(hs, logstashAuthHeader);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, hs);

    int still_running;
    curl_multi_add_handle(multi_handle,handle);
    CURLMcode mc = curl_multi_perform(multi_handle, &still_running);
    if (mc) {
        LogCurlMultiError("ERROR: curl_multi_perform failed", mc);
        return;
    }
}

ExitCode Logstash_Init(EventLoop *eventLoopInstance, char *password)
{
    eventLoop = eventLoopInstance;
    logstashPassword = password;

    CurlInit();

    static const struct timespec pollingInterval = { .tv_sec = 1, .tv_nsec = 0 };
    curlTimer = CreateEventLoopPeriodicTimer(eventLoop, &CurlTimerEventHandler, &pollingInterval);
    if (curlTimer == NULL) {
        return ExitCode_WebClientInit_CurlTimer;
    }

    return ExitCode_Success;
}

void Logstash_Fini(void)
{
    CurlFini();
    DisposeEventLoopTimer(curlTimer);
}
