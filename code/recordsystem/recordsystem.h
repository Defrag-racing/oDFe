// recordsystem.h -- thread and string utility functions for record system

#ifndef __RECORDSYSTEM_H__
#define __RECORDSYSTEM_H__

#include <pthread.h>
#include <string.h>
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"


/*
===============
startsWith

Returns qtrue if the string begins with the given prefix
===============
*/
qboolean startsWith(const char *string, const char *prefix);

/*
===============
CheckForRS

Checks if a string starts with "ClientBegin: " and performs actions
===============
*/
void RS_Gateway(const char *s);

/*
===============
Sys_CreateThread

Create a new thread of execution with no arguments
===============
*/
void Sys_CreateThread(void (*function)(const char *), const char *arg);

void RS_CreateRecord(const char *s);

void RS_GameSendServerCommand( int clientNum, const char *text );

qboolean RS_IsCommand(const char *s);

void RS_CommandGateway(const char *s);

qboolean RS_IsClientTimerStop( const char *s);

typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

/*
===============
WriteMemoryCallback

Callback function for storing HTTP response data
===============
*/
// size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

/*
===============
RS_HttpGet

Performs an HTTP GET request to the specified URL
Returns the response as a null-terminated string that must be freed by the caller
Returns NULL if the request failed
===============
*/
char* RS_HttpGet(const char *url);

#endif // __RECORDSYSTEM_H__