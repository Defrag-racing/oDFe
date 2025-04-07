#include "../server/server.h"
#include "cJSON.h"

static void RS_Top(client_t *client, const char *str) {
    apiResponse_t *response;
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
    Com_sprintf(url, sizeof(url), "http://%s/api/commands/top?client_num=%d&cmd_string=%s&curr_map=%s", 
                "149.28.120.254:8000", clientNum, encoded_str, encoded_map);
    
    // Free encoded strings when done
    free(encoded_str);
    free(encoded_map);
    
    // Make the HTTP request
    response = RS_ParseAPIResponse(RS_HttpGet(url));
    
    if (response) {
        RS_PrintAPIResponse(response, qfalse);
        free(response); // Free the response
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to get response\n\"");
    }
}

static void RS_Recent(client_t *client, const char *str) {
    apiResponse_t *response;
    char *encoded_str;
    char url[512];
    int clientNum = client - svs.clients;
    
    encoded_str = RS_UrlEncode(str);
    if (!encoded_str) {
        RS_GameSendServerCommand(clientNum, "print \"^1Error encoding command\n\"");
        return;
    }
    
    // Build the URL

    Com_sprintf(url, sizeof(url), "http://%s/api/commands/recent?client_num=%d&cmd_string=%s", "149.28.120.254:8000", clientNum, encoded_str);
    free(encoded_str); // Free encoded string when done
    
    // Make the HTTP request
    response = RS_ParseAPIResponse(RS_HttpGet(url));
    
    if (response) {
        RS_PrintAPIResponse(response, qfalse);
        free(response); // Free the response
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Failed to get response\n\"");
    }
}

static void RS_Login(client_t *client, const char *str) {
    char *responseString;
    char *jsonString;
    cJSON *json;
    int clientNum = client - svs.clients;
    apiResponse_t *response;
    char url[512];
    
    // Create a JSON object
    json = cJSON_CreateObject();
    if (!json) {
        RS_GameSendServerCommand(clientNum, "print \"^1Internal engine error, contact server admin.\n\"");
        Com_DPrintf("RS_ERROR: Couldn't create JSON Object for string: %s\n", str );
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
        RS_GameSendServerCommand(clientNum, "print \"^1Internal engine error, contact server admin.\n\"");
        Com_DPrintf("RS_ERROR: Couldn't convert JSON object to string for string: %s\n", str );
        return;
    }
    
    client->awaitingLogin = qtrue;
    Com_sprintf(url, sizeof(url), "http://%s/api/commands/login", "149.28.120.254:8000");
    // Make the HTTP request
    responseString = RS_HttpPost(url, "application/json", jsonString);
    
    free(jsonString);
    
    response = RS_ParseAPIResponse(responseString);

    if (response) {
        if (response->success && strlen(response->uuid) > 0 && strlen(response->displayName) > 0) {
            client->loggedIn = client->awaitingLogin; // Make sure client player is the same one that was awaiting login
            strncpy(client->uuid, response->uuid, UUID_LENGTH);
            strncpy(client->displayName, response->displayName, MAX_NAME_LENGTH);      
        }
        RS_PrintAPIResponse(response, qtrue);      
    } else {
        RS_GameSendServerCommand(clientNum, "print \"^1Bad response from server, contact defrag.racing admins\n\"");
        Com_DPrintf("RS_ERROR: Couldn't parse response json: %s\n", jsonString );
    }

    client->awaitingLogin = qfalse;
    free(response);
}

static void RS_Logout(client_t *client, const char *str) {
    int clientNum = client - svs.clients;

    if (client->loggedIn == qfalse) {
        RS_GameSendServerCommand(clientNum, va("print \"%s^5, ^7You are not logged in^5.\n\"", client->name));
        return;
    }
    client->loggedIn = qfalse; // Log them out locally, don't wait for server.
    strcpy(client->uuid, "");
    strcpy(client->displayName, "");
    RS_GameSendServerCommand(clientNum, va("print \"%s^5, ^7You are now logged out^5.\n\"", client->name));
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
            Sys_CreateThread(modules[i].handler, client, s);
            return qtrue;
        }
    }
    
    // If we reach here, no command matched
    return qfalse;
}
