#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/gpio.h>
#include <applibs/log.h>

#include "eventloop_timer_utilities.h"
#include "log_utils.h"

#include "logstash.h"
#include "upload.h"

static datablock_t* dataBlock = NULL;
static EventLoopTimer *uploadTimer = NULL;
static EventLoop *eventLoop = NULL; // not owned

static int qsort_comp(const void* elem1, const void* elem2)
{
    int f = *((int*)elem1);
    int s = *((int*)elem2);
    if (f > s) return  1;
    if (f < s) return -1;
    return 0;
}

static void UploadTimerEventHandler(EventLoopTimer *timer)
{
    char buffer[256];

    if (ConsumeEventLoopTimerEvent(timer) != 0) {
        LogErrno("ERROR: cannot consume the timer event");
        return;
    }

    Log_Debug("Uploading data\n");

    if (dataBlock->cpmMessagesReceived >= 59) {
        // Geiger counter has been running for a full minute
        // (allow one missing message to account for timing mismatch)
        snprintf(buffer, sizeof(buffer), "{ \"cpm\": %d }", dataBlock->cpm);
        Log_Debug("%s\n", buffer);
        SendToLogstash("https://logstash.saintgimp.org/geiger", buffer);
    }
    else {
        // Geiger counter is probably not running
        Log_Debug("cpm not valid\n");
    }
    dataBlock->cpmMessagesReceived = 0;

    if (dataBlock->pressureSamplesReceived >= 590)
    {
        // Pressure sensor has been running for a full minute
        // (allow a few missing samples to account for timing mismatch)
        // TODO: the logic to turn the sample set into final numbers should live
        // in the pressure module
        qsort(dataBlock->pressureSamples, dataBlock->pressureSamplesReceived, sizeof(*dataBlock->pressureSamples), qsort_comp);
        uint32_t pressure = dataBlock->pressureSamples[dataBlock->pressureSamplesReceived / 2];
        float altitude_meters = 95;
        uint32_t seaLevelPressure = (uint32_t)(pressure / pow(1.0 - altitude_meters / 44330, 5.255));
        
        snprintf(buffer, sizeof(buffer), "{ \"pressure\": %d, \"sea_level_pressure\": %d }", pressure, seaLevelPressure);
        Log_Debug("%s\n", buffer);
        SendToLogstash("https://logstash.saintgimp.org/pressure", buffer);

        Log_Debug("Number of pressure samples = %d\n", dataBlock->pressureSamplesReceived);
    }
    else {
        // Pressure sensor is probably not running
        Log_Debug("Pressure not valid\n");
    }
    dataBlock->pressureSamplesReceived = 0;
}

ExitCode Upload_Init(EventLoop *eventLoopInstance, datablock_t * dataBlockInstance)
{
    eventLoop = eventLoopInstance;
    dataBlock = dataBlockInstance;

    static const struct timespec uploadInterval = {.tv_sec = 60, .tv_nsec = 0};
    uploadTimer = CreateEventLoopPeriodicTimer(eventLoop, &UploadTimerEventHandler,
                                                    &uploadInterval);

    if (uploadTimer == NULL) {
        return ExitCode_UploadInit_Timer;
    }

    return ExitCode_Success;
}

void Upload_Fini(void)
{
    DisposeEventLoopTimer(uploadTimer);
}
