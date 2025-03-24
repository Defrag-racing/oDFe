// recordsystem.h -- thread and string utility functions for record system

#ifndef __RECORDSYSTEM_H__
#define __RECORDSYSTEM_H__

#include <pthread.h>
#include <string.h>
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

// String utility functions
qboolean startsWith(const char *string, const char *prefix);
qboolean endsWith(const char *string, const char *suffix);

/*
===============
RS_Gateway

Main entry point for record system events
===============
*/
void RS_Gateway(const char *s);

/*
===============
Sys_CreateThread

Create a new thread of execution with a string argument
===============
*/
void Sys_CreateThread(void (*function)(const char *), const char *arg);

/*
===============
RS_CreateRecord

Creates a record entry from a ClientTimerStop event
===============
*/
void RS_CreateRecord(const char *s);

/*
===============
RS_GameSendServerCommand

Wrapper for SV_SendServerCommand
===============
*/
void RS_GameSendServerCommand(int clientNum, const char *text);

/*
===============
RS_CommandGateway

Routes commands to their appropriate handlers
===============
*/
void RS_CommandGateway(int clientNum, const char *plyrName, const char *s);

/*
===============
RS_IsClientTimerStop

Checks if a string represents a client timer stop event
===============
*/
qboolean RS_IsClientTimerStop(const char *s);

/*
===============
Memory structure for HTTP responses
===============
*/
typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

/*
===============
RS_HttpGet

Performs an HTTP GET request to the specified URL
Returns the response as a null-terminated string that must be freed by the caller
Returns NULL if the request failed
===============
*/
char* RS_HttpGet(const char *url);

/*
===============
RS_HttpPost

Performs an HTTP POST request to the specified URL with the given payload
Returns the response as a null-terminated string that must be freed by the caller
Returns NULL if the request failed
===============
*/
char* RS_HttpPost(const char *url, const char *contentType, const char *payload);

/*
===============
RS_UrlEncode

Encodes a string for use in a URL
The returned string must be freed by the caller
===============
*/
char* RS_UrlEncode(const char *str);

#endif // __RECORDSYSTEM_H__