#include "recordsystem.h"

void RS_Gateway(const char *s) {
    if (RS_IsClientTimerStop(s)) {
        if (Cvar_VariableIntegerValue("sv_cheats") != 0) {
            return;
        }
        Sys_CreateThread(RS_CreateRecord, s);
    }
}