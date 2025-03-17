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
void Sys_CreateThread(void (*function)(void));

void RS_CreateRecord(void);

#endif // __RECORDSYSTEM_H__