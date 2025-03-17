#include "recordsystem.h"
#include "../server/server.h"

/*
===============
SV_GameSendServerCommand

Sends a command string to a client
===============
*/
static void RS_GameSendServerCommand( int clientNum, const char *text ) {
	if ( clientNum == -1 ) {
		SV_SendServerCommand( NULL, "%s", text );
	} else {
		if ( clientNum < 0 || clientNum >= sv.maxclients ) {
			return;
		}
		SV_SendServerCommand( svs.clients + clientNum, "%s", text );
	}
}

void RS_CreateRecord(void){
  Sys_Sleep(5000);
  RS_GameSendServerCommand( -1, "print \"^5You have finished\"\n" );
}

