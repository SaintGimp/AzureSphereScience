#pragma once

#include "main.h"
void SendToLogstash(char* url, char* postBody);
ExitCode Logstash_Init(EventLoop *eventLoopInstance, char *password);
void Logstash_Fini(void);
