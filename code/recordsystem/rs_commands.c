#include "recordsystem.h"
#include "../server/server.h"


static void RS_Top(const char *str) {
    char *response;
    
    // Make the HTTP request
    response = RS_HttpGet(va("http://localhost:8000/api/commands/top?saystr=%s&curr_map=%s", RS_UrlEncode(str), sv_mapname->string));
    
    if (response) {
        return RS_GameSendServerCommand( -1, va("print \"%s\"", response));
        free(response);
    } else {
        return RS_GameSendServerCommand( -1, "print \"Failed to get response\n\"" );
    }
}

static void RS_Recent(const char *str) {
    char *response;
    
    // Make the HTTP request
    response = RS_HttpGet(va("http://localhost:8000/api/commands/recent?saystr=%s", RS_UrlEncode(str)));
    
    if (response) {
        return RS_GameSendServerCommand( -1, va("print \"%s\"", response));
        free(response);
    } else {
        return RS_GameSendServerCommand( -1, "print \"Failed to get response\n\"" );
    }
}

static void RS_Login(const char *map) {
    return RS_GameSendServerCommand( -1, "print \"^5You are now logged in\n\"" );
}

static void RS_Logout(const char *map) {
    return RS_GameSendServerCommand( -1, "print \"^5You are now logged out\n\"" );
}

typedef struct {
    const char *suffix;
    void (*handler)(const char *);
} Module;

Module modules[] = {
    {": !top\n", RS_Top},
    {": !recent\n", RS_Recent},
    {": !login\n", RS_Login},
    {": !logout\n", RS_Logout}
};

qboolean RS_IsCommand(const char *string) {
    if (!string) {
        return qfalse;
    }
    
    // Check if it starts with "say:"
    if (!startsWith(string, "say:")) {
        return qfalse;
    }
    
    int numModules= sizeof(modules) / sizeof(modules[0]);
    for (int i = 0; i < numModules; i++) {
        if (endsWith(string, modules[i].suffix)) {
            return qtrue;
        }
    }
    
    return qfalse;
}


void RS_CommandGateway(const char *string) {
    // Check each command pattern
    int numModules= sizeof(modules) / sizeof(modules[0]);
    for (int i = 0; i < numModules; i++) {
        if (endsWith(string, modules[i].suffix)) {
            // Call the appropriate handler function
            modules[i].handler(string);
            return;
        }
    }
    return RS_GameSendServerCommand( -1, "print \"^5Command Received\n\"" );
}
