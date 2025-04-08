#include "../server/server.h"
#include <curl/curl.h>
#include "cJSON.h"


qboolean startsWith(const char *string, const char *prefix) {
    if (!string || !prefix) {
        return qfalse;
    }
    
    size_t prefixLen = strlen(prefix);
    size_t stringLen = strlen(string);
    
    if (prefixLen > stringLen) {
        return qfalse;
    }
    
    return (strncmp(string, prefix, prefixLen) == 0) ? qtrue : qfalse;
}

qboolean endsWith(const char *string, const char *suffix) {
    if (!string || !suffix) {
        return qfalse;
    }
    
    size_t stringLen = strlen(string);
    size_t suffixLen = strlen(suffix);
    
    if (suffixLen > stringLen) {
        return qfalse;
    }
    
    return (strcmp(string + stringLen - suffixLen, suffix) == 0) ? qtrue : qfalse;
}

// Structure to hold the thread arguments
typedef struct {
    client_t *client_arg;
    char *str_arg;
    void (*function)(client_t *, const char *);
} thread_args_t;

// Wrapper function that unpacks arguments and calls the target function
static void *thread_wrapper(void *data) {
    thread_args_t *args = (thread_args_t *)data;
    
    // Call the actual function with the unpacked arguments
    args->function(args->client_arg, args->str_arg);
    
    // Clean up
    if (args->str_arg) {
        free(args->str_arg);
    }
    free(args);
    
    return NULL;
}

/*
===============
Sys_CreateThread

Create a new thread of execution with client data and a string argument
===============
*/
void Sys_CreateThread(void (*function)(client_t *, const char *), client_t *client, const char *arg) {
    // We need to duplicate the string argument to ensure it remains valid
    // for the duration of the thread
    char *arg_copy = NULL;
    
    if (arg) {
        arg_copy = strdup(arg);
        if (!arg_copy) {
            Com_Error(ERR_FATAL, "Sys_CreateThread: Failed to allocate memory for thread argument");
            return;
        }
    }
    
    // Create and populate the argument structure
    thread_args_t *args = (thread_args_t *)malloc(sizeof(thread_args_t));
    if (!args) {
        if (arg_copy) {
            free(arg_copy);
        }
        Com_Error(ERR_FATAL, "Sys_CreateThread: Failed to allocate memory for thread arguments");
        return;
    }
    
    args->client_arg = client;  // Pass client by reference
    args->str_arg = arg_copy;
    args->function = function;
    
    pthread_t threadHandle;
    pthread_attr_t attr;
    int result;
    
    pthread_attr_init(&attr);
    // Create the thread in detached state so its resources are automatically
    // freed when it exits
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    result = pthread_create(&threadHandle, 
                           &attr, 
                           thread_wrapper,
                           args);
    
    pthread_attr_destroy(&attr);
    
    if (result != 0) {
        if (args) {
            if (args->str_arg) {
                free(args->str_arg);
            }
            free(args);
        }
        Com_Error(ERR_FATAL, "Sys_CreateThread: pthread_create failed with error %d", result);
    }
}

void RS_GameSendServerCommand(int clientNum, const char *text) {
    if (clientNum == -1) {
        SV_SendServerCommand(NULL, "%s", text);
    } else {
        if (clientNum < 0 || clientNum >= sv.maxclients) {
            return;
        }
        SV_SendServerCommand(svs.clients + clientNum, "%s", text);
    }
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        // Out of memory
        Com_Printf("RS: Not enough memory for HTTP response\n");
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;  // Null terminate
    
    return realsize;
}

/*
===============
RS_HttpGet

Performs an HTTP GET request to the specified URL
Returns the response as a null-terminated string that must be freed by the caller
Returns NULL if the request failed
===============
*/
char* RS_HttpGet(const char *url) {
    CURL *curl;
    CURLcode res;
    MemoryStruct chunk;
    char *response = NULL;
    
    // Initialize memory structure
    chunk.memory = malloc(1);
    if (!chunk.memory) {
        Com_Printf("RS: Failed to allocate memory for HTTP response\n");
        return NULL;
    }
    
    chunk.size = 0;
    
    // Initialize cURL
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    
    if (!curl) {
        Com_Printf("RS: Failed to initialize cURL handle\n");
        free(chunk.memory);
        curl_global_cleanup();
        return NULL;
    }
    
    // Set options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Quake III Record System");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);  // 10 second timeout
    
    // Perform the request
    Com_Printf("RS: Making HTTP GET request to %s\n", url);
    res = curl_easy_perform(curl);
    
    // Check for errors
    if (res != CURLE_OK) {
        Com_Printf("RS: HTTP GET failed: %s\n", curl_easy_strerror(res));
        free(chunk.memory);
    } else {
        // Request successful
        Com_Printf("RS: HTTP GET successful (%lu bytes)\n", (unsigned long)chunk.size);
        response = chunk.memory;
    }
    
    // Clean up
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    return response;
}

/*
===============
RS_UrlEncode

Encodes a string for use in a URL
The returned string must be freed by the caller
===============
*/
char* RS_UrlEncode(const char *str) {
    if (!str) {
        return NULL;
    }
    
    // Characters that don't need encoding
    const char *safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
    
    // Count the number of characters that need encoding
    int len = strlen(str);
    int encoded_len = 0;
    int i;
    
    for (i = 0; i < len; i++) {
        if (strchr(safe, str[i])) {
            encoded_len++;
        } else {
            encoded_len += 3; // %XX format for each unsafe character
        }
    }
    
    // Allocate memory for the encoded string
    char *encoded = (char *)malloc(encoded_len + 1);
    if (!encoded) {
        return NULL;
    }
    
    // Encode the string
    char *p = encoded;
    for (i = 0; i < len; i++) {
        if (strchr(safe, str[i])) {
            *p++ = str[i];
        } else {
            sprintf(p, "%%%02X", (unsigned char)str[i]);
            p += 3;
        }
    }
    *p = '\0';
    
    return encoded;
}

/*
===============
RS_HttpPost

Performs an HTTP POST request to the specified URL with the given payload
Parameters:
  url - The target URL for the POST request
  contentType - The content type of the payload (e.g., "application/json")
  payload - The data to send in the POST request
Returns the response as a null-terminated string that must be freed by the caller
Returns NULL if the request failed
===============
*/
char* RS_HttpPost(const char *url, const char *contentType, const char *payload) {
    CURL *curl;
    CURLcode res;
    MemoryStruct chunk;
    char *response = NULL;
    
    // Initialize memory structure
    chunk.memory = malloc(1);
    if (!chunk.memory) {
        Com_Printf("RS: Failed to allocate memory for HTTP response\n");
        return NULL;
    }
    
    chunk.size = 0;
    
    // Initialize cURL
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    
    if (!curl) {
        Com_Printf("RS: Failed to initialize cURL handle\n");
        free(chunk.memory);
        curl_global_cleanup();
        return NULL;
    }
    
    // Create headers list
    struct curl_slist *headers = NULL;
    if (contentType) {
        char contentTypeHeader[256];
        Com_sprintf(contentTypeHeader, sizeof(contentTypeHeader), "Content-Type: %s", contentType);
        headers = curl_slist_append(headers, contentTypeHeader);
    }
    
    // Log endpoint
    Com_Printf("RS: Endpoint: %s\n", url);
    
    // Log headers
    Com_Printf("RS: Headers:\n");
    struct curl_slist *temp = headers;
    while (temp) {
        Com_Printf("  %s\n", temp->data);
        temp = temp->next;
    }
    
    // Log payload
    Com_Printf("RS: Payload: %s\n", payload ? payload : "(none)");
    
    // Set options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Quake III Record System");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);  // 10 second timeout
    
    // Perform the request
    Com_Printf("RS: Making HTTP POST request to %s\n", url);
    res = curl_easy_perform(curl);
    
    // Check for errors
    if (res != CURLE_OK) {
        Com_Printf("RS: HTTP POST failed: %s\n", curl_easy_strerror(res));
        free(chunk.memory);
    } else {
        // Request successful
        Com_Printf("RS: HTTP POST successful (%lu bytes)\n", (unsigned long)chunk.size);
        response = chunk.memory;
    }
    
    // Clean up
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    return response;
}


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cJSON.h"

// Function to parse JSON response into apiResponse_t structure
apiResponse_t *RS_ParseAPIResponse(const char* jsonString) {
    // Allocate memory for the response structure
    apiResponse_t *response = (apiResponse_t*)malloc(sizeof(apiResponse_t));
    if (!response) {
        return NULL; // Memory allocation failed
    }
    
    // Initialize with default values
    response->success = 0;
    response->targetClientNum = 0;
    response->message = NULL;
    memset(response->displayName, 0, MAX_NAME_LENGTH);
    memset(response->uuid, 0, UUID_LENGTH);
    
    // Parse JSON string
    cJSON* json = cJSON_Parse(jsonString);
    if (!json) {
        free(response);
        return NULL; // JSON parsing failed
    }
    
    // Extract success field (integer)
    cJSON* successField = cJSON_GetObjectItem(json, "success");
    if (cJSON_IsNumber(successField)) {
        response->success = successField->valueint;
    }
    
    // Extract targetClientNumfield (integer)
    cJSON* targetClientNumField = cJSON_GetObjectItem(json, "targetClientNum");
    if (cJSON_IsNumber(targetClientNumField)) {
        response->targetClientNum = targetClientNumField->valueint;
    }
    
    // Extract message field (string array)
    cJSON* messageField = cJSON_GetObjectItem(json, "message");
    if (cJSON_IsString(messageField) && messageField->valuestring && strlen(messageField->valuestring) > 0) {
        response->message = strdup(messageField->valuestring);
    }
    
    // Extract displayName field (string array)
    cJSON* displayNameField = cJSON_GetObjectItem(json, "displayName");
    if (cJSON_IsString(displayNameField) && displayNameField->valuestring) {
        strncpy(response->displayName, displayNameField->valuestring, MAX_NAME_LENGTH - 1);
        response->displayName[MAX_NAME_LENGTH - 1] = '\0'; // Ensure null termination
    }
    
    // Extract uuid field (string array)
    cJSON* uuidField = cJSON_GetObjectItem(json, "uuid");
    if (cJSON_IsString(uuidField) && uuidField->valuestring) {
        strncpy(response->uuid, uuidField->valuestring, UUID_LENGTH - 1);
        response->uuid[UUID_LENGTH - 1] = '\0'; // Ensure null termination
    }

    // Clean up JSON object
    cJSON_Delete(json);
    
    return response;
}

void RS_PrintAPIResponse(apiResponse_t *response, qboolean mentionClient) {
    const char *finalMessage="";
    const char *mentionPrefix="";
    client_t *targetClient;

    if (response->targetClientNum < -1) {
        Com_DPrintf("RS: %s", response->message);
        return;
    }

    if (mentionClient && response->targetClientNum >= 0) {
        targetClient = &svs.clients[response->targetClientNum];
        strlen(targetClient->name) > 0 ? mentionPrefix = va("%s", targetClient->name) : "";
    }

    if (response->message != NULL) {
        finalMessage = va("%s%s", mentionPrefix, response->message);
        RS_GameSendServerCommand(response->targetClientNum, va("print \"^5(^7defrag^5.^7racing^5)^7 %s\n\"", finalMessage));
    }
}

char* formatTime(int ms) {
    static char timeString[12]; // Increased buffer size to be safe
    
    int minutes = ms / 60000;
    ms %= 60000;
    
    int seconds = ms / 1000;
    ms %= 1000;
    
    // Format as MM.SS.MMM (with leading zeros)
    snprintf(timeString, sizeof(timeString), "%02d.%02d.%03d", minutes, seconds, ms);
    
    return timeString;
}

// dfState_t RS_GetDFState(client_t *client) {
//     char stats[MAX_STATS];
//     stats = client->ps.stats;
// }