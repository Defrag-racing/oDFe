#include "recordsystem.h"

void RS_Gateway(const char *s) {
    
    if (RS_IsClientTimerStop(s)) {
        if (Cvar_VariableIntegerValue("sv_cheats") != 0) {
            return;
        }
        Sys_CreateThread((void (*)(void))RS_CreateRecord);
    }
    
    if (RS_IsCommand(s)) {
        // Additional checks or logic for this case
        Sys_CreateThread((void (*)(void))RS_CommandGateway);
    }
    
    return;
}
