#include "../server/server.h"
#include "cJSON.h"

/*
====================
ParseTimerStop
Parses a timer stop log message into a structured format
====================
*/

static timeInfo_t* RS_ParseClientTimerStop(const char* logLine) {
    if (!logLine) {
        return NULL;
    }
    
    // Validate the prefix "ClientTimerStop:" (no color codes allowed between words and colon)
    if (strncmp(logLine, "ClientTimerStop", 15) != 0) {
        return NULL;
    }
    
    const char *colonPos = strchr(logLine, ':');
    if (!colonPos) {
        return NULL;
    }
    
    // Check for non-whitespace between "ClientTimerStop" and ":"
    for (const char *p = logLine + 15; p < colonPos; p++) {
        if (*p != ' ' && *p != '\t') {
            return NULL;
        }
    }
    
    // Allocate and initialize the info structure
    timeInfo_t* info = (timeInfo_t*)Z_Malloc(sizeof(timeInfo_t));
    if (!info) {
        return NULL;
    }
    memset(info, 0, sizeof(timeInfo_t));
    
    // Skip past "ClientTimerStop:" and any whitespace
    logLine = colonPos + 1;
    while (*logLine && *logLine == ' ') logLine++;
    
    // Make a copy of the line to tokenize
    char buffer[1024];
    Q_strncpyz(buffer, logLine, sizeof(buffer));
    const char *str = buffer;
    const char *token;
    
    // Parse client number
    token = COM_Parse(&str);
    if (!token[0] || strlen(token) > 2 || strchr(token, '^') != NULL) {
        Z_Free(info);
        return NULL;
    }
    
    info->clientNum = atoi(token);
    if (info->clientNum < 0 || info->clientNum >= MAX_CLIENTS) {
        Z_Free(info);
        return NULL;
    }
    
    // Parse time
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->time = atoi(token);
    
    // Parse mapname (can't contain a colon)
    token = COM_ParseExt(&str, qtrue);
    if (!token[0] || strchr(token, ':') != NULL) {
        Z_Free(info);
        return NULL;
    }
    Q_strncpyz(info->mapname, token, sizeof(info->mapname));
    
    // Parse player name (can contain a colon only if quoted)
    const char* rawStr = str;
    token = COM_ParseExt(&str, qtrue);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    
    // Check if name was quoted
    qboolean wasQuoted = qfalse;
    while (*rawStr && (*rawStr == ' ' || *rawStr == '\t')) rawStr++;
    if (*rawStr == '"') {
        wasQuoted = qtrue;
    }
    
    // Reject unquoted names with colons
    if (!wasQuoted && strchr(token, ':')) {
        Z_Free(info);
        return NULL;
    }
    Q_strncpyz(info->name, token, sizeof(info->name));
    
    // Helper macro to parse and validate numeric fields
    #define PARSE_NUMERIC_FIELD(field) do { \
        token = COM_Parse(&str); \
        if (!token[0] || strchr(token, ':') != NULL) { \
            Z_Free(info); \
            return NULL; \
        } \
        info->field = atoi(token); \
    } while(0)
    
    // Parse remaining numeric fields
    PARSE_NUMERIC_FIELD(gametype);
    PARSE_NUMERIC_FIELD(promode);
    PARSE_NUMERIC_FIELD(submode);
    PARSE_NUMERIC_FIELD(interferenceOff);
    PARSE_NUMERIC_FIELD(obEnabled);
    PARSE_NUMERIC_FIELD(version);
    
    // Parse date (special case as it's a string)
    token = COM_Parse(&str);
    if (!token[0] || strchr(token, ':') != NULL) {
        Z_Free(info);
        return NULL;
    }
    Q_strncpyz(info->date, token, sizeof(info->date));
    
    return info;
}

/*
===============
RS_SendTime

Sends a time record to the API server
===============
*/
static void RS_SendTime(client_t *client, const char *cmdString) {
    apiResponse_t *response;
    char *jsonString;
    cJSON *json;
    char url[512];

    timeInfo_t *timeInfo = RS_ParseClientTimerStop(cmdString);
    
    // Create a JSON object for the request
    json = cJSON_CreateObject();
    if (!json) {
        return;
    }
    
    // Add properties to the JSON object
    cJSON_AddStringToObject(json, "cmdString", cmdString);
    cJSON_AddStringToObject(json, "uuid", svs.clients[timeInfo->clientNum].uuid);
    
    // Convert JSON object to string
    jsonString = cJSON_Print(json);
    cJSON_Delete(json); // Free the JSON object
    Com_DPrintf("json payload: %s\n", jsonString);

    Com_sprintf(url, sizeof(url), "http://%s/api/records", "149.28.120.254:8000");

    // Make the HTTP request
    response = RS_ParseAPIResponse(RS_HttpPost(url, "application/json", jsonString));
    
    // Free the JSON string
    free(jsonString);
    
    if (response) {
        RS_PrintAPIResponse(response, qtrue);
        free(response);
    } else {
        RS_GameSendServerCommand(timeInfo->clientNum, "print \"^1Failed to connect to record server\n\"");
    }
}

void RS_Gateway(const char *s) {
    timeInfo_t *timeInfo = RS_ParseClientTimerStop(s);
    if (timeInfo && Cvar_VariableIntegerValue("sv_cheats") == 0) {

        // if (timeInfo->clientNum >= 0 && timeInfo->clientNum < MAX_CLIENTS) {
        //     return;
        // }

        client_t *client = &svs.clients[timeInfo->clientNum];
        // if (startsWith(client->name, "ClientTimerStop")) {
        //     return;
        // }

        Com_DPrintf("Client timer stop detected for client %i with time %i\n", timeInfo->clientNum, timeInfo->time);

        if (client->loggedIn) {
            client->awaitingDemoSave = qtrue;
            client->timerStopTime = svs.time;
            client->timeInfo = timeInfo;
            Sys_CreateThread(RS_SendTime, client, s);
        }
        else {
            RS_GameSendServerCommand(timeInfo->clientNum, "print \"^7You are not logged in^5.\n\"");
        }
    }
}
