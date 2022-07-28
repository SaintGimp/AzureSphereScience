// This minimal Azure Sphere app repeatedly toggles an LED. Use this app to test that
// installation of the device and SDK succeeded, and that you can build, deploy, and debug an app.

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/log.h>
#include <applibs/eventloop.h>

#include "eventloop_timer_utilities.h"
#include "main.h"
#include "logstash.h"
#include "geiger.h"
#include "bmp180.h"
#include "upload.h"

static void ParseCommandLineArguments(int argc, char* argv[]);

static char logstashPassword[32];
static datablock_t dataBlock = {
    .cpm = 0,
    .cpmMessagesReceived = 0,
    .pressureSamplesReceived = 0,
};
static EventLoop* eventLoop = NULL;

// Termination state
static volatile sig_atomic_t exitCode = ExitCode_Success;

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
    // Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
    exitCode = ExitCode_TermHandler_SigTerm;
}

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>
///     ExitCode_Success if all resources were allocated successfully; otherwise another
///     ExitCode value which indicates the specific failure.
/// </returns>
static ExitCode InitPeripheralsAndHandlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

    eventLoop = EventLoop_Create();
    if (eventLoop == NULL) {
        Log_Debug("Could not create event loop.\n");
        return ExitCode_Init_EventLoop;
    }

    ExitCode localExitCode = Upload_Init(eventLoop, &dataBlock);
    if (localExitCode != ExitCode_Success) {
        return localExitCode;
    }

    localExitCode = Geiger_Init(eventLoop, &dataBlock);
    if (localExitCode != ExitCode_Success) {
        return localExitCode;
    }

    localExitCode = Bmp180_Init(eventLoop, &dataBlock);
    if (localExitCode != ExitCode_Success) {
        return localExitCode;
    }

    localExitCode = Logstash_Init(eventLoop, logstashPassword);
    if (localExitCode != ExitCode_Success) {
        return localExitCode;
    }

    return ExitCode_Success;
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
void ClosePeripheralsAndHandlers(void)
{
    // Release resources.
    Bmp180_Fini();
    Geiger_Fini();
    Upload_Fini();
    Logstash_Fini();

    EventLoop_Close(eventLoop);
}

/// <summary>
///     Parse the command-line arguments given in the application manifest.
/// </summary>
static void ParseCommandLineArguments(int argc, char* argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1)
    {
        switch (opt)
        {
        case 'p':
            strncpy(logstashPassword, optarg, sizeof(logstashPassword));
            break;
        default:
            break;
        }
    }
}

int main(int argc, char** argv)
{
    Log_Debug("Azure Sphere GimpScience starting.\n");

    ParseCommandLineArguments(argc, argv);

    exitCode = InitPeripheralsAndHandlers();

    // Use event loop to wait for events and trigger handlers, until an error or SIGTERM happens
    while (exitCode == ExitCode_Success) {
        EventLoop_Run_Result result = EventLoop_Run(eventLoop, -1, true);
        // Continue if interrupted by signal, e.g. due to breakpoint being set.
        if (result == EventLoop_Run_Failed && errno != EINTR) {
            exitCode = ExitCode_Main_EventLoopFail;
        }
    }

    ClosePeripheralsAndHandlers();

    Log_Debug("Application exiting.\n");
    return exitCode;
}
