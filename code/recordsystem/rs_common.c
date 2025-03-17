#include "recordsystem.h"
#include "../server/server.h"

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
void Sys_CreateThread(void (*function)(void)) {
    // POSIX implementation (Linux, macOS, etc.)
    pthread_t threadHandle;
    pthread_attr_t attr;
    int result;
    
    pthread_attr_init(&attr);
    // Create the thread in detached state so its resources are automatically
    // freed when it exits
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    result = pthread_create(&threadHandle, 
                           &attr, 
                           (void*(*)(void*))function, 
                           NULL);
    
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
