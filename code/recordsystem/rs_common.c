#include "recordsystem.h"
#include "../server/server.h"
#include <curl/curl.h>


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

/*
===============
Sys_CreateThread

Create a new thread of execution
===============
*/
void Sys_CreateThread(void (*function)(const char *), const char *arg) {
    pthread_t threadHandle;
    pthread_attr_t attr;
    int result;
    
    pthread_attr_init(&attr);
    // Create the thread in detached state so its resources are automatically
    // freed when it exits
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    // Cast the function pointer and pass the character as an integer cast to void*
    result = pthread_create(&threadHandle, 
                           &attr, 
                           (void*(*)(void*))function,
                           (void*)(intptr_t)arg);
    
    pthread_attr_destroy(&attr);
    
    if (result != 0) {
        Com_Error(ERR_FATAL, "Sys_CreateThread: pthread_create failed with error %d", result);
    }
}

void RS_GameSendServerCommand( int clientNum, const char *text ) {
	if ( clientNum == -1 ) {
		SV_SendServerCommand( NULL, "%s", text );
	} else {
		if ( clientNum < 0 || clientNum >= sv.maxclients ) {
			return;
		}
		SV_SendServerCommand( svs.clients + clientNum, "%s", text );
	}
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
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