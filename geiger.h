/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#pragma once

#include <applibs/eventloop.h>
#include "main.h"

ExitCode Geiger_Init(EventLoop *eventLoopInstance, datablock_t *dataBlock);
void Geiger_Fini(void);
