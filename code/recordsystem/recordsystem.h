// recordsystem.h -- thread and string utility functions for record system

#ifndef __RECORDSYSTEM_H__
#define __RECORDSYSTEM_H__

#include <pthread.h>
#include <string.h>

// String utility functions
qboolean startsWith(const char *string, const char *prefix);
qboolean endsWith(const char *string, const char *suffix);

typedef struct {
    int success;
    int targetClientNum;
    char *message;
	char displayName[MAX_NAME_LENGTH];
    char uuid[UUID_LENGTH];
} apiResponse_t;

typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

void RS_Gateway(const char *s);
void Sys_CreateThread(void (*function)(client_t *, const char *), client_t *client, const char *arg);
void RS_GameSendServerCommand(int clientNum, const char *text);
qboolean RS_ExecuteClientCommand(client_t *client, const char *s);
qboolean RS_IsClientTimerStop(const char *s);
char* formatTime(int ms);
char* RS_HttpGet(const char *url);
char* RS_HttpPost(const char *url, const char *contentType, const char *payload);
char* RS_UrlEncode(const char *str);
apiResponse_t* RS_ParseAPIResponse(const char* jsonString);
void RS_PrintAPIResponse(apiResponse_t *response, qboolean mentionClient);
void RS_StartRecord(client_t *client);
void RS_StopRecord(client_t *client);
void RS_WriteGamestate( client_t *client);
void RS_WriteSnapshot(client_t *client);
void RS_SaveDemo(client_t *client);
void RS_DemoHandler(client_t *client);

#endif // __RECORDSYSTEM_H__