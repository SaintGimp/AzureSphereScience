/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/gpio.h>
#include <applibs/uart.h>
#include <applibs/log.h>

#include "eventloop_timer_utilities.h"
#include "log_utils.h"

#include "geiger.h"

static int uartFd = -1;
static uint8_t messageBuffer[1024];
static size_t messageBytesReceived = 0;
static datablock_t *dataBlock = NULL;
static EventRegistration* uartEventReg = NULL;
static EventLoop *eventLoop = NULL; // not owned

static void UartEventHandler(EventLoop* el, int fd, EventLoop_IoEvents events, void* context)
{
    const size_t receiveBufferSize = 256;
    uint8_t receiveBuffer[receiveBufferSize + 1]; // allow extra byte for string termination
    ssize_t bytesRead;

    // Read incoming UART data. It is expected behavior that messages may be received in multiple
    // partial chunks.
    bytesRead = read(uartFd, receiveBuffer, receiveBufferSize);
    if (bytesRead == -1) {
        Log_Debug("ERROR: Could not read UART: %s (%d).\n", strerror(errno), errno);
        return;
    }

    if (bytesRead > 0) {
        // Copy this fragment into the message buffer
        // TODO: calc min of bytes read or message buffer left
        strncpy(messageBuffer + messageBytesReceived, receiveBuffer, (size_t)bytesRead);
        messageBytesReceived += (size_t)bytesRead;

        // Null terminate the read buffer to make it a valid string, and print it
        receiveBuffer[bytesRead] = 0;
        //Log_Debug("UART received %d bytes: '%s'\n", bytesRead, (char*)receiveBuffer);

        if (messageBuffer[messageBytesReceived - 1] == '\n') {
            messageBuffer[messageBytesReceived] = 0;
            //Log_Debug("Message has %d bytes: '%s'\n", messageBytesReceived, (char*)messageBuffer);
            messageBytesReceived = 0;

            const char delimiters[2] = ",";
            char* token = NULL;
            token = strtok(messageBuffer, delimiters);
            for (int x = 1; x < 4; x++)
            {
                token = strtok(NULL, delimiters);
            }
            if (token) {
                int cpm = atoi(token);
                dataBlock->cpm = (uint8_t)cpm;
                dataBlock->cpmMessagesReceived++;
            }
        }
    }
}

ExitCode Geiger_Init(EventLoop *eventLoopInstance, datablock_t *dataBlockInstance)
{
    eventLoop = eventLoopInstance;
    dataBlock = dataBlockInstance;

    // Create a UART_Config object, open the UART and set up UART event handler
    UART_Config uartConfig;
    UART_InitConfig(&uartConfig);
    uartConfig.baudRate = 9600;
    uartConfig.dataBits = UART_DataBits_Eight;
    uartConfig.parity = UART_Parity_None;
    uartConfig.stopBits = UART_StopBits_One;
    uartConfig.flowControl = UART_FlowControl_None;
    uartFd = UART_Open(SEEED_MT3620_MDB_J1_ISU0_UART, &uartConfig);
    if (uartFd == -1) {
        Log_Debug("ERROR: Could not open UART: %s (%d).\n", strerror(errno), errno);
        return ExitCode_Init_UartOpen;
    }
    uartEventReg = EventLoop_RegisterIo(eventLoop, uartFd, EventLoop_Input, UartEventHandler, NULL);
    if (uartEventReg == NULL) {
        return ExitCode_Init_RegisterIo;
    }
    return ExitCode_Success;
}

void Geiger_Fini(void)
{
    EventLoop_UnregisterIo(eventLoop, uartEventReg);

    Log_Debug("Closing file descriptors.\n");
    CloseFdAndLogOnError(uartFd, "Uart");
}
