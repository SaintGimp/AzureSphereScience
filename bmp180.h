#pragma once

#include <applibs/eventloop.h>
#include "main.h"

ExitCode Bmp180_Init(EventLoop* eventLoopInstance, datablock_t* dataBlockInstance);
void Bmp180_Fini(void);