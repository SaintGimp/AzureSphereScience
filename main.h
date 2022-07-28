/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

// This header file defines the values which are used in multiple source files.

#pragma once

#include <hw/seeed_mt3620_mdb.h>

/// <summary>
/// Termination codes for this application. These are used for the
/// application exit code. They must all be between zero and 255,
/// where zero is reserved for successful termination.
/// </summary>
typedef enum {
    ExitCode_Success = 0,

    ExitCode_TermHandler_SigTerm = 1,

    ExitCode_Init_EventLoop = 2,

    ExitCode_Main_EventLoopFail = 3,

    ExitCode_UploadInit_Timer = 100,

    ExitCode_Init_UartOpen = 200,
    ExitCode_Init_RegisterIo = 201,
    ExitCode_UartEvent_Read = 202,
    ExitCode_Uart_Write = 203,

    ExitCode_WebClientInit_CurlTimer = 300,

    ExitCode_CurlInit_GlobalInit = 400,
    ExitCode_CurlInit_MultiInit = 401,
    ExitCode_CurlInit_MultiSetOptSocketFunction = 402,
    ExitCode_CurlInit_MultiSetOptTimerFunction = 403,

    ExitCode_CurlSetupEasy_EasyInit = 500,
    ExitCode_CurlSetupEasy_OptUrl = 501,
    ExitCode_CurlSetupEasy_OptFollowLocation = 502,
    ExitCode_CurlSetupEasy_OptProtocols = 503,
    ExitCode_CurlSetupEasy_OptRedirProtocols = 504,
    ExitCode_CurlSetupEasy_OptWriteFunction = 505,
    ExitCode_CurlSetupEasy_OptWriteData = 506,
    ExitCode_CurlSetupEasy_OptHeaderData = 507,
    ExitCode_CurlSetupEasy_OptUserAgent = 508,
    ExitCode_CurlSetupEasy_StoragePath = 509,
    ExitCode_CurlSetupEasy_CAInfo = 510,
    ExitCode_CurlSetupEasy_Verbose = 511,
    ExitCode_CurlSetupEasy_CurlSetDefaultProxy = 512,

    ExitCode_Init_OpenMaster = 600,
    ExitCode_Init_SetBusSpeed = 601,
    ExitCode_Init_SetTimeout = 602,
    ExitCode_BPM180_Initialize = 603,
} ExitCode;

typedef struct DataBlock {
    uint8_t cpm;
    uint8_t cpmMessagesReceived;
    uint32_t pressureSamples[1024];
    uint32_t pressureSamplesReceived;
} datablock_t;