// recordsystem.h -- thread and string utility functions for record system

#ifndef __RECORDSYSTEM_H__
#define __RECORDSYSTEM_H__

#include <pthread.h>
#include <string.h>
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../server/server.h"

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


void RS_SendTime(const char *cmdString);

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
qboolean RS_CommandGateway(int clientNum, const char *plyrName, const char *s);

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


void RS_PrintAPIResponse(const char *jsonString);

void RS_StartRecord( int clientNum, const char *plyrName, const char *str );


/*
====================
SV_StopRecording

stop recording a demo
====================
*/
void RS_StopRecord( int clientNum, const char *plyrName, const char *str );

/*
====================
SV_WriteGamestate
====================
*/
void RS_WriteGamestate( client_t *client);

/*
====================
RS_WriteSnapshotToDemo
====================
*/
void RS_WriteSnapshot(client_t *client);
void RS_WriteDemoMessage(client_t *client, msg_t *msg);

// Message types for demo recording
typedef enum {
    DEMO_MSG_GAMESTATE,
    DEMO_MSG_SNAPSHOT,
    DEMO_MSG_END_RECORDING
} demoMsgType_t;

// Structure for a demo message in the queue
typedef struct {
    demoMsgType_t type;       // Type of message
    int clientNum;            // Client index
    int sequence;             // Sequence number
    int serverTime;           // Server time for this message
    byte *data;               // Message data
    int dataSize;             // Size of the message data
} demoQueuedMsg_t;

// Initialize the threaded demo writer system
qboolean RS_InitThreadedDemos(void);

// Shutdown the threaded demo writer system
void RS_ShutdownThreadedDemos(void);

// Start recording a demo for a client (threaded version)
void RS_StartThreadedRecord(int clientNum, const char *plyrName, const char *demoName);

// Stop recording a demo for a client (threaded version)
void RS_StopThreadedRecord(int clientNum, const char *plyrName, const char *str);

// Queue a gamestate message for writing
void RS_QueueGamestate(client_t *client);

// Queue a snapshot message for writing
void RS_QueueSnapshot(client_t *client);

// Get number of pending messages in queue
int RS_GetQueuedMessageCount(void);


#endif // __RECORDSYSTEM_H__