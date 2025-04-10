#include "../server/server.h"
#include "cJSON.h"

/*
====================
ParseTimerStop
Parses a timer stop log message into a structured format
====================
*/

static timeInfo_t* RS_ParseClientTimerStop(const char* logLine) {
    timeInfo_t* info;
    char buffer[1024];
    const char *token, *str;
    
    // Check that the line starts with "ClientTimerStop:" without color codes in between
    if (!logLine) {
        return NULL;
    }
    
    // Validate the prefix to ensure there are no color codes between "ClientTimerStop" and ":"
    if (strncmp(logLine, "ClientTimerStop", 15) != 0) {
        return NULL;
    }
    
    // Find the position of the colon
    const char *colonPos = strchr(logLine, ':');
    if (!colonPos) {
        return NULL;
    }
    
    // Check for any characters between "ClientTimerStop" and ":" that aren't spaces
    for (const char *p = logLine + 15; p < colonPos; p++) {
        if (*p != ' ' && *p != '\t') {
            // Found a non-space character (could be ^7 or other color code)
            return NULL;
        }
    }
    
    // Allocate memory for the structure
    info = (timeInfo_t*)Z_Malloc(sizeof(timeInfo_t));
    if (!info) {
        return NULL;
    }
    
    // Initialize the structure
    memset(info, 0, sizeof(timeInfo_t));
    
    // Skip past "ClientTimerStop:"
    logLine = colonPos + 1;
    while (*logLine && *logLine == ' ') logLine++; // Skip any extra spaces
    
    // Make a copy of the line to tokenize
    Q_strncpyz(buffer, logLine, sizeof(buffer));
    str = buffer;
    
    // Parse client number
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    
    // Validate client number: must be at most 2 characters, no carets
    if (strlen(token) > 2 || strchr(token, '^') != NULL) {
        Z_Free(info);
        return NULL;
    }
    
    info->clientNum = atoi(token);
    
    // Check that client number is in valid range
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
    
    // Parse mapname
    token = COM_ParseExt(&str, qtrue); // Use qtrue to handle quoted strings properly
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    Q_strncpyz(info->mapname, token, sizeof(info->mapname));
    
    // Parse netname and check for colon in unquoted name
    const char* rawStr = str; // Save position before parsing to check quotes
    token = COM_ParseExt(&str, qtrue);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    
    // Check if the name was quoted in the original string
    qboolean wasQuoted = qfalse;
    while (*rawStr && (*rawStr == ' ' || *rawStr == '\t')) rawStr++; // Skip whitespace
    if (*rawStr == '"') {
        wasQuoted = qtrue;
    }
    
    // Check for unquoted name containing a colon
    if (!wasQuoted && strchr(token, ':')) {
        // Unquoted name contains a colon - reject this line
        Z_Free(info);
        return NULL;
    }
    
    Q_strncpyz(info->name, token, sizeof(info->name));
    
    // Parse gametype
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->gametype = atoi(token);
    
    // Parse promode
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->promode = atoi(token);
    
    // Parse submode
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->submode = atoi(token);
    
    // Parse interference flag
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->interferenceOff = atoi(token);
    
    // Parse OB flag
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->obEnabled = atoi(token);
    
    // Parse version
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->version = atoi(token);
    
    // Parse date
    token = COM_Parse(&str);
    if (!token[0]) {
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

        Com_DPrintf("Client timer stop detected for client %i with time %i\n", timeInfo->clientNum, timeInfo->time);

        client_t *client = &svs.clients[timeInfo->clientNum];
        if (client->loggedIn) {
            client->awaitingDemoSave = qtrue;
            client->timerStopTime = svs.time;
            client->timerStopInfo = timeInfo;
            Sys_CreateThread(RS_SendTime, client, s);
        }
        else {
            RS_GameSendServerCommand(timeInfo->clientNum, "print \"^7You are not logged in^5.\n\"");
            RS_StopRecord(client);
        }
    }
}
