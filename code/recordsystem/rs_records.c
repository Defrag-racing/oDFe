#include "recordsystem.h"
#include "cJSON.h"

/*
===============
RS_SendTime

Sends a time record to the API server
===============
*/
void RS_SendTime(const char *cmdString) {
    char *response;
    char *jsonString;
    cJSON *json;

    
    // Create a JSON object for the request
    json = cJSON_CreateObject();
    if (!json) {
        return;
    }
    
    // Add properties to the JSON object
    cJSON_AddStringToObject(json, "cmdString", cmdString);
    
    // Convert JSON object to string
    jsonString = cJSON_Print(json);
    cJSON_Delete(json); // Free the JSON object
    
    // Make the HTTP request
    response = RS_HttpPost("http://localhost:8000/api/records", 
                           "application/json", jsonString);
    
    // Free the JSON string
    free(jsonString);
    
    if (response) {
        RS_PrintAPIResponse(response);
        free(response);
    } else {
        RS_GameSendServerCommand(-1, "print \"^1Failed to connect to record server\n\"");
    }
}

qboolean RS_IsClientTimerStop(const char *s) {
    // Extra logic here to make sure it's a true timer stop
    // Potential: compare playerstates between this and last frame
    // Check for timer state bit and that it's non-zero
    return startsWith(s, "ClientTimerStop: ") ? qtrue : qfalse;
}
