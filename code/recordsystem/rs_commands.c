#include "../server/server.h"
#include "cJSON.h"

static void RS_Top(client_t *client, const char *str) {
    char *response;
    char *encoded_str;
    char *encoded_map;
    char url[512];
    int clientNum = client - svs.clients;
    
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
        RS_ProcessAPIResponse(client, response);
        free(response); // Free the response
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to get response\n\"");
    }
}

static void RS_Recent(client_t *client, const char *str) {
    char *response;
    char *encoded_str;
    char url[512];
    int clientNum = client - svs.clients;
    
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
        RS_ProcessAPIResponse(client, response);
        free(response); // Free the response
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to get response\n\"");
    }
}

static void RS_Login(client_t *client, const char *str) {
    char *response;
    char *jsonString;
    cJSON *json;
    int clientNum = client - svs.clients;
    
    // Create a JSON object
    json = cJSON_CreateObject();
    if (!json) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error creating JSON object\n\"");
        return;
    }
    
    // Add client number and command string to the JSON object
    cJSON_AddNumberToObject(json, "clientNum", clientNum);
    cJSON_AddStringToObject(json, "cmdString", str);
    cJSON_AddStringToObject(json, "plyrName", client->name);
    
    // Convert JSON object to string
    jsonString = cJSON_Print(json);
    cJSON_Delete(json); // Free the JSON object
    
    if (!jsonString) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error serializing JSON\n\"");
        return;
    }
    
    client->awaitingLogin = qtrue;
    // Make the HTTP request
    response = RS_HttpPost("http://localhost:8000/api/commands/login", 
                          "application/json", jsonString);
    
    // Free the JSON string
    free(jsonString);
    
    if (response) {
        RS_ProcessAPIResponse(client, response);
        free(response);
        client->loggedIn = qtrue;
        Com_sprintf(client->uuid, MAX_NAME_LENGTH, "1a2b3c4d-5e6f-7a8b-9c0d-1e2f3a4b5c6d");
        Com_sprintf(client->displayName, MAX_NAME_LENGTH, "Frog");
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to connect to server\n\"");
    }
    
    client->awaitingLogin = qfalse;
}

static void RS_Logout(client_t *client, const char *str) {
    char *response;
    char *jsonString;
    cJSON *json;
    int clientNum = client - svs.clients;

    client->loggedIn = qfalse; // Log them out locally, don't wait for server.
    RS_GameSendServerCommand(clientNum, va("print \"%s^5, ^7you are now logged out^5.^7\n\"", client->name));
    
    // Create a JSON object
    json = cJSON_CreateObject();
    if (!json) {
        Com_Printf("^1Error creating JSON object\n");
        return;
    }
    
    // Add client number and command string to the JSON object
    cJSON_AddNumberToObject(json, "clientNum", clientNum);
    cJSON_AddStringToObject(json, "cmdString", str);
    cJSON_AddStringToObject(json, "plyrName", client->name);
    
    // Convert JSON object to string
    jsonString = cJSON_Print(json);
    cJSON_Delete(json); // Free the JSON object
    
    if (!jsonString) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error serializing JSON\n\"");
        return;
    }
    
    client->awaitingLogout = qtrue; // Let game know that client is waiting for remote logout
    // Make the HTTP request
    response = RS_HttpPost("http://localhost:8000/api/commands/logout", 
                          "application/json", jsonString);
    
    // Free the JSON string
    free(jsonString);
    
    if (response) {
        RS_ProcessAPIResponse(client, response);
        free(response);
        client->awaitingLogout = qfalse;
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to connect to server\n\"");
    }
}

typedef struct {
    const char *pattern;
    void (*handler)(client_t *client, const char *str);
} Module;

static Module modules[] = {
    {"top", RS_Top},
    {"recent", RS_Recent},
    {"login", RS_Login},
    {"logout", RS_Logout}
};

qboolean RS_ExecuteClientCommand(client_t *client, const char *s) {
    // Check each command pattern
    int numModules = sizeof(modules) / sizeof(modules[0]);
    for (int i = 0; i < numModules; i++) {
        if (startsWith(s, va("%s ",modules[i].pattern)) || Q_stricmp(s, modules[i].pattern) == 0) {
            // Call the appropriate handler function
            modules[i].handler(client, s);
            return qtrue;
        }
    }
    
    // If we reach here, no command matched
    return qfalse;
}