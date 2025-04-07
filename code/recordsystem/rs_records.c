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
    
    // Check that the line starts with "ClientTimerStop:"
    if (!logLine || strncmp(logLine, "ClientTimerStop:", 16) != 0) {
        return NULL;
    }
    
    // Allocate memory for the structure
    info = (timeInfo_t*)Z_Malloc(sizeof(timeInfo_t));
    if (!info) {
        return NULL;
    }
    
    // Initialize the structure
    memset(info, 0, sizeof(timeInfo_t));
    
    // Skip past "ClientTimerStop: "
    logLine += 16;
    
    // Make a copy of the line to tokenize
    Q_strncpyz(buffer, logLine, sizeof(buffer));
    str = buffer;
    
    // Parse client number
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->clientNum = atoi(token);
    
    // Parse time
    token = COM_Parse(&str);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    info->time = atoi(token);
    
    // Parse mapname (quoted)
    token = COM_ParseExt(&str, qfalse);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    // Remove quotes
    if (token[0] == '"') {
        Q_strncpyz(info->mapname, token + 1, sizeof(info->mapname) - 1);
        if (info->mapname[strlen(info->mapname) - 1] == '"') {
            info->mapname[strlen(info->mapname) - 1] = '\0';
        }
    } else {
        Q_strncpyz(info->mapname, token, sizeof(info->mapname));
    }
    
    // Parse netname (quoted)
    token = COM_ParseExt(&str, qfalse);
    if (!token[0]) {
        Z_Free(info);
        return NULL;
    }
    // Remove quotes
    if (token[0] == '"') {
        Q_strncpyz(info->name, token + 1, sizeof(info->name) - 1);
        if (info->name[strlen(info->name) - 1] == '"') {
            info->name[strlen(info->name) - 1] = '\0';
        }
    } else {
        Q_strncpyz(info->name, token, sizeof(info->name));
    }
    
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
static void RS_SendTime(client_t client, const char *cmdString) {
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
