#include "recordsystem.h"
#include "cJSON.h"

static void RS_Top(int clientNum, const char *plyrName, const char *str) {
    char *response;
    char *encoded_str;
    char *encoded_map;
    char url[512];
    
    // Encode the command string
    encoded_str = RS_UrlEncode(str);
    if (!encoded_str) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error encoding command\n\"");
        return;
    }
    
    // Encode the map name
    encoded_map = RS_UrlEncode(sv_mapname->string);
    if (!encoded_map) {
        free(encoded_str);  // Free already allocated memory
        RS_GameSendServerCommand(clientNum, "print \"^1Error encoding map name\n\"");
        return;
    }
    
    // Build the URL
    Com_sprintf(url, sizeof(url), "http://localhost:8000/api/commands/top?client_num=%d&cmd_string=%s&curr_map=%s", 
                clientNum, encoded_str, encoded_map);
    
    // Free encoded strings when done
    free(encoded_str);
    free(encoded_map);
    
    // Make the HTTP request
    response = RS_HttpGet(url);
    
    if (response) {
        RS_PrintAPIResponse(response);
        free(response); // Free the response
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to get response\n\"");
    }
}

static void RS_Recent(int clientNum, const char *plyrName, const char *str) {
    char *response;
    char *encoded_str;
    char url[512];
    
    encoded_str = RS_UrlEncode(str);
    if (!encoded_str) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error encoding command\n\"");
        return;
    }
    
    // Build the URL

    Com_sprintf(url, sizeof(url), "http://localhost:8000/api/commands/recent?client_num=%d&cmd_string=%s", clientNum, encoded_str);
    free(encoded_str); // Free encoded string when done
    
    // Make the HTTP request
    response = RS_HttpGet(url);
    
    if (response) {
        RS_PrintAPIResponse(response);
        free(response); // Free the response
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to get response\n\"");
    }
}

static void RS_Login(int clientNum, const char *plyrName, const char *str) {
    char *response;
    char *jsonString;
    cJSON *json;
    
    // Create a JSON object
    json = cJSON_CreateObject();
    if (!json) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error creating JSON object\n\"");
        return;
    }
    
    // Add client number and command string to the JSON object
    cJSON_AddNumberToObject(json, "clientNum", clientNum);
    cJSON_AddStringToObject(json, "cmdString", str);
    cJSON_AddStringToObject(json, "plyrName", plyrName);
    
    // Convert JSON object to string
    jsonString = cJSON_Print(json);
    cJSON_Delete(json); // Free the JSON object
    
    if (!jsonString) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error serializing JSON\n\"");
        return;
    }
    
    // Make the HTTP request
    response = RS_HttpPost("http://localhost:8000/api/commands/login", 
                          "application/json", jsonString);
    
    // Free the JSON string
    free(jsonString);
    
    if (response) {
        RS_PrintAPIResponse(response);
        free(response);
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to connect to server\n\"");
    }
}

static void RS_Logout(int clientNum, const char *plyrName, const char *str) {
    char *response;
    char *jsonString;
    cJSON *json;
    
    // Create a JSON object
    json = cJSON_CreateObject();
    if (!json) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error creating JSON object\n\"");
        return;
    }
    
    // Add client number and command string to the JSON object
    cJSON_AddNumberToObject(json, "clientNum", clientNum);
    cJSON_AddStringToObject(json, "cmdString", str);
    cJSON_AddStringToObject(json, "plyrName", plyrName);
    
    // Convert JSON object to string
    jsonString = cJSON_Print(json);
    cJSON_Delete(json); // Free the JSON object
    
    if (!jsonString) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error serializing JSON\n\"");
        return;
    }
    
    // Make the HTTP request
    response = RS_HttpPost("http://localhost:8000/api/commands/logout", 
                          "application/json", jsonString);
    
    // Free the JSON string
    free(jsonString);
    
    if (response) {
        RS_PrintAPIResponse(response);
        free(response);
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to connect to server\n\"");
    }
}

typedef struct {
    const char *pattern;
    void (*handler)(int clientNum, const char *plyrName, const char *str);
} Module;

static Module modules[] = {
    {"top", RS_Top},
    {"recent", RS_Recent},
    {"login", RS_Login},
    {"logout", RS_Logout},
    {"rs_record", RS_StartThreadedRecord},
    {"rs_stoprecord", RS_StopThreadedRecord}
};

qboolean RS_CommandGateway(int clientNum, const char *plyrName, const char *s) {
    // Check each command pattern
    int numModules = sizeof(modules) / sizeof(modules[0]);
    for (int i = 0; i < numModules; i++) {
        if (startsWith(s, va("%s ",modules[i].pattern)) || Q_stricmp(s, modules[i].pattern) == 0) {
            // Call the appropriate handler function
            modules[i].handler(clientNum, plyrName, s);
            return qtrue;
        }
    }
    
    // If we reach here, no command matched
    return qfalse;
}