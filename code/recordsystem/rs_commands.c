#include "recordsystem.h"
#include "../server/server.h"

static void RS_Top(int clientNum, const char *plyrName, const char *str) {
    char *response;
    char *encoded_str;
    char *encoded_map;
    char url[512];
    
    // Encode the command string
    encoded_str = RS_UrlEncode(str);
    if (!encoded_str) {
        RS_GameSendServerCommand(-1, "print \"^1Error encoding command\n\"");
        return;
    }
    
    // Encode the map name
    encoded_map = RS_UrlEncode(sv_mapname->string);
    if (!encoded_map) {
        free(encoded_str);  // Free already allocated memory
        RS_GameSendServerCommand(-1, "print \"^1Error encoding map name\n\"");
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
        RS_GameSendServerCommand(-1, va("print \"%s\"", response));
        free(response); // Free the response
    } else {
        RS_GameSendServerCommand(-1, "print \"^1Failed to get response\n\"");
    }
}

static void RS_Recent(int clientNum, const char *plyrName, const char *str) {
    char *response;
    char *encoded_str;
    char url[512];
    
    encoded_str = RS_UrlEncode(str);
    if (!encoded_str) {
        RS_GameSendServerCommand(-1, "print \"^1Error encoding command\n\"");
        return;
    }
    
    // Build the URL
    Com_sprintf(url, sizeof(url), "http://localhost:8000/api/commands/recent?saystr=%s", encoded_str);
    free(encoded_str); // Free encoded string when done
    
    // Make the HTTP request
    response = RS_HttpGet(url);
    
    if (response) {
        RS_GameSendServerCommand(-1, va("print \"%s\"", response));
        free(response); // Free the response
    } else {
        RS_GameSendServerCommand(-1, "print \"^1Failed to get response\n\"");
    }
}

static void RS_Login(int clientNum, const char *plyrName, const char *str) {
    char *response;
    char payload[512];
    
    // Create JSON payload with the entire command string
    Com_sprintf(payload, sizeof(payload), 
                "{\"saystr\":\"%s\"}", str);
    
    // Make the HTTP request
    response = RS_HttpPost("http://localhost:8000/api/commands/login", 
                           "application/json", payload);
    
    if (response) {
        RS_GameSendServerCommand(-1, va("print \"%s\"", response));
        free(response);
    } else {
        RS_GameSendServerCommand(-1, "print \"^1Failed to connect to server\n\"");
    }
}

static void RS_Logout(int clientNum, const char *plyrName, const char *str) {
    char *response;
    char payload[512];
    
    // Create JSON payload with the entire command string
    Com_sprintf(payload, sizeof(payload), 
                "{\"saystr\":\"%s\"}", str);
    
    // Make the HTTP request
    response = RS_HttpPost("http://localhost:8000/api/commands/logout", 
                           "application/json", payload);
    
    if (response) {
        RS_GameSendServerCommand(-1, va("print \"%s\"", response));
        free(response);
    } else {
        RS_GameSendServerCommand(-1, "print \"^1Failed to connect to server\n\"");
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
    {"logout", RS_Logout}
};

void RS_CommandGateway(int clientNum, const char *plyrName, const char *s) {
    // Check each command pattern
    int numModules = sizeof(modules) / sizeof(modules[0]);
    for (int i = 0; i < numModules; i++) {
        if (startsWith(s, modules[i].pattern)) {
            // Call the appropriate handler function
            modules[i].handler(clientNum, plyrName, s);
            return;
        }
    }
    
    // If we reach here, no command matched
    RS_GameSendServerCommand(-1, "print \"^5Command Received but not recognized\n\"");
}