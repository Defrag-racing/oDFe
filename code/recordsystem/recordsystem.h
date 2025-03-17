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

#endif // __RECORDSYSTEM_H__