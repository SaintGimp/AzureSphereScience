#pragma once

#include <applibs/eventloop.h>
#include "main.h"

ExitCode Upload_Init(EventLoop *eventLoopInstance, datablock_t * dataBlockInstance);
void Upload_Fini(void);
