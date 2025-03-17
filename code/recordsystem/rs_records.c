#include "recordsystem.h"

qboolean RS_IsClientTimerStop( const char *s) {
	// extra logic here to make sure it's a true timer stop
	// potential: compare playerstates between this and last frame:  
	// check for timer state bit and that it's non-zero
	return startsWith(s, "ClientTimerStop: ") ? qtrue: qfalse;
}

void RS_CreateRecord(const char *s){
  RS_GameSendServerCommand( -1, "print \"^5You have finished\n\"" );
}
