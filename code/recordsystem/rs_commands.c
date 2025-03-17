#include "recordsystem.h"

qboolean RS_IsCommand(const char *s) {
    // parse string for client commands
    return startsWith(s, "say: frog: !") ? qtrue: qfalse;
}

void RS_CommandGateway(void) {
  return RS_GameSendServerCommand( -1, "print \"^5Command Received\n\"" );
}