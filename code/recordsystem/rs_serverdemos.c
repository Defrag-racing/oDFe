#include "../server/server.h"

// Static storage for delta compression
static void RS_EmitPacketEntities( const clientSnapshot_t *from, const clientSnapshot_t *to, msg_t *msg, entityState_t *oldents );

/*
====================
RS_StartRecord

Begins recording a demo for a given client
====================
*/
void RS_StartRecord(client_t *client) {
    char demoName[MAX_OSPATH];
    int clientNum = client - svs.clients;

    if (client->state != CS_ACTIVE) {
        return;
    }

    if (client->isRecording) {
        Com_Printf("Already recording client %i\n", clientNum);
        return;
    }

    // Create demo name
    Q_strncpyz(demoName, va("demos/[%d].dm_68", clientNum), sizeof(demoName));

    Com_Printf("recording to %s.\n", demoName);

    // Start writing to demo file
    client->demoFile = FS_FOpenFileWrite(demoName);
    if (client->demoFile == FS_INVALID_HANDLE) {
        Com_Printf("ERROR: couldn't open file: %s.\n", demoName);
        return;
    }

    Q_strncpyz(client->demoName, demoName, sizeof(demoName));
    // Set client's demo flags
    client->isRecording = qtrue;
    // client->demoWaiting = qtrue;

	// write out the gamestate message
	RS_WriteGamestate( client );
}

/*
====================
RS_SaveDemo

Save a demo on a logged clientTimerStop
====================
*/
void RS_SaveDemo(client_t *client) {
    int clientNum = client - svs.clients;
    char finalName[MAX_OSPATH];
    timeInfo_t *timeInfo = client->timerStopInfo;

    if (!client->isRecording) {
        Com_Printf("Client %i is not being recorded\n", clientNum);
        return;
    }

    if (client->demoFile != FS_INVALID_HANDLE) {
        int len;

        // Write proper EOF markers - TWO -1 values
        len = -1;
        FS_Write(&len, 4, client->demoFile);
        FS_Write(&len, 4, client->demoFile);
        
        FS_FCloseFile(client->demoFile);
        client->demoFile = FS_INVALID_HANDLE;
        Com_Printf("Stopped recording client %i\n", clientNum);
    }

    if (timeInfo->gametype == 1) { // run mode
        Com_sprintf( finalName, sizeof( finalName ), "demos/%s[mdf.%s]%s[%s][%s].dm_68", \
        timeInfo->mapname, \
        timeInfo->promode ? "cpm" : "vq3", \
        formatTime(timeInfo->time), \
        client->displayName, \
        client->uuid);
    }
    else {
        Com_sprintf( finalName, sizeof( finalName ), "demos/%s[mfc.%s.%i]%s[%s][%s].dm_68", \
        timeInfo->mapname, \
        timeInfo->promode ? "cpm" : "vq3", \
        timeInfo->submode, \
        formatTime(timeInfo->time), \
        client->displayName, \
        client->uuid);
    }

    if (client->demoFile != FS_INVALID_HANDLE) {
        int len;

        // Write proper EOF markers - TWO -1 values
        len = -1;
        FS_Write(&len, 4, client->demoFile);
        FS_Write(&len, 4, client->demoFile);
        
        FS_FCloseFile(client->demoFile);
        client->demoFile = FS_INVALID_HANDLE;
        Com_Printf("Stopped recording client %i\n", clientNum);
    }

    FS_Rename( client->demoName, finalName );
    Com_Printf("Saved demo: %s\n", finalName);
    client->awaitingDemoSave = qfalse;
    client->demoFile = FS_INVALID_HANDLE;
    client->isRecording = qfalse;
}

/*
====================
RS_StopRecord

stop recording a demo
====================
*/
void RS_StopRecord(client_t *client) {
    if (client->awaitingDemoSave)
        return RS_SaveDemo(client);
    int clientNum = client - svs.clients;

    if (!client->isRecording) {
        Com_Printf("Client %i is not being recorded\n", clientNum);
        return;
    }

    if (client->demoFile != FS_INVALID_HANDLE) {
        int len;

        // Write proper EOF markers - TWO -1 values
        len = -1;
        FS_Write(&len, 4, client->demoFile);
        FS_Write(&len, 4, client->demoFile);
        
        FS_FCloseFile(client->demoFile);
        client->demoFile = FS_INVALID_HANDLE;
        Com_Printf("Stopped recording client %i\n", clientNum);
    }

    client->isRecording = qfalse;
}

/*
====================
RS_WriteGamestate
====================
*/
void RS_WriteGamestate(client_t *client) {
	int			i;
	const svEntity_t *svEnt;
	msg_t		msg;
	byte		msgBuffer[ MAX_MSGLEN_BUF ];
    int len;
    // entityState_t *ent;
    entityState_t nullstate;

	// accept usercmds starting from current server time only
	// Com_Memset( &client->lastUsercmd, 0x0, sizeof( client->lastUsercmd ) );
	// client->lastUsercmd.serverTime = sv.time - 1;

	MSG_Init( &msg, msgBuffer, MAX_MSGLEN );
    MSG_Bitstream( &msg );

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

    client->demoMessageSequence = 1;
    client->demoCommandSequence = client->reliableSequence;
	// send any server commands waiting to be sent first.
	// we have to do this cause we send the client->reliableSequence
	// with a gamestate and it sets the clc.serverCommandSequence at
	// the client side
	// SV_UpdateServerCommandsToClient( client, &msg );

    client->demoDeltaNum = 0;
	// send the gamestate
	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, client->reliableSequence );

	// write the configstrings
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( *sv.configstrings[ i ] != '\0' ) {
			MSG_WriteByte( &msg, svc_configstring );
			MSG_WriteShort( &msg, i );
			if ( i == CS_SYSTEMINFO && sv.pure != sv_pure->integer ) {
				// make sure we send latched sv.pure, not forced cvar value
				char systemInfo[BIG_INFO_STRING];
				Q_strncpyz( systemInfo, sv.configstrings[ i ], sizeof( systemInfo ) );
				Info_SetValueForKey_s( systemInfo, sizeof( systemInfo ), "sv_pure", va( "%i", sv.pure ) );
				MSG_WriteBigString( &msg, systemInfo );
			} else {
				MSG_WriteBigString( &msg, sv.configstrings[i] );
			}
		}
	}

	// write the baselines
	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( i = 0 ; i < MAX_GENTITIES; i++ ) {
        if ( !sv.baselineUsed[i] ) {
			continue;
		}
        svEnt = &sv.svEntities[i];
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, &svEnt->baseline, qtrue );
	}

	MSG_WriteByte( &msg, svc_EOF );

	MSG_WriteLong( &msg, client - svs.clients ); // client num

	// write the checksum feed
	MSG_WriteLong( &msg, sv.checksumFeed );

	// it is important to handle gamestate overflow
	// but at this stage client can't process any reliable commands
	// so at least try to inform him in console and release connection slot
	// if ( msg.overflowed ) {
	// 	if ( client->netchan.remoteAddress.type == NA_LOOPBACK ) {
	// 		Com_Error( ERR_DROP, "gamestate overflow" );
	// 	} else {
	// 		NET_OutOfBandPrint( NS_SERVER, &client->netchan.remoteAddress, "print\n" S_COLOR_RED "SERVER ERROR: gamestate overflow\n" );
	// 		SV_DropClient( client, "gamestate overflow" );
	// 	}
	// 	return;
	// }

    // Finalize message
    MSG_WriteByte(&msg, svc_EOF);

    // Write to demo file
    // Sequence should be properly set to match client expectation
    len = LittleLong(0);  // Gamestate uses sequence 0
    FS_Write(&len, 4, client->demoFile);

    len = LittleLong(msg.cursize);

    FS_Write(&len, 4, client->demoFile);

    FS_Write(msg.data, msg.cursize, client->demoFile);
}

/*
====================
RS_WriteServerCommands
====================
*/
static void RS_WriteServerCommands(msg_t *msg, client_t *client) {
    int i;

    // Write all commands since the last recorded command
    if (client->reliableSequence - client->demoCommandSequence > 0) {
        // Don't write more than MAX_RELIABLE_COMMANDS
        if (client->reliableSequence - client->demoCommandSequence > MAX_RELIABLE_COMMANDS) {
            client->demoCommandSequence = client->reliableSequence - MAX_RELIABLE_COMMANDS;
        }

        for (i = client->demoCommandSequence + 1; i <= client->reliableSequence; i++) {
            int index = i & (MAX_RELIABLE_COMMANDS - 1);
            MSG_WriteByte(msg, svc_serverCommand);
            MSG_WriteLong(msg, i);
            MSG_WriteString(msg, client->reliableCommands[index]);
        }
    }

    client->demoCommandSequence = client->reliableSequence;
}

/*
====================
RS_WriteSnapshot
====================
*/
void RS_WriteSnapshot(client_t *client) {
    byte bufData[MAX_MSGLEN_BUF];
    clientSnapshot_t *oldsnap;
    playerState_t *oldps;
    msg_t msg;
    int i, len;
    int	snapFlags;
    
    clientSnapshot_t *snap;
    // Get current snapshot
    snap = &client->frames[client->netchan.outgoingSequence & PACKET_MASK];
    // Com_DPrintf("Writing snapshot to client: %i\n", frame->frameNum);

	MSG_Init( &msg, bufData, MAX_MSGLEN );
	MSG_Bitstream( &msg );

    // Write reliable sequence
    MSG_WriteLong(&msg, client->reliableSequence);
    
    // Write server commands
    RS_WriteServerCommands(&msg, client);
    
    MSG_WriteByte( &msg, svc_snapshot );
	MSG_WriteLong( &msg, sv.time ); // sv.time
	MSG_WriteByte( &msg, client->demoDeltaNum ); // 0 or 1

    snapFlags = svs.snapFlagServerBit;
	if ( client->rateDelayed ) {
		snapFlags |= SNAPFLAG_RATE_DELAYED;
	}
	if ( client->state != CS_ACTIVE ) {
		snapFlags |= SNAPFLAG_NOT_ACTIVE;
	}
	MSG_WriteByte( &msg, snapFlags );  // snapFlags. NOTE: in client-side it's snap->snapFlags which doesn't exist in clientSnapshot_t.
	MSG_WriteByte( &msg, snap->areabytes );  // areabytes
	MSG_WriteData( &msg, snap->areabits, snap->areabytes ); // NOTE: snap->areabits is snap->areamask in client side.
	
    if ( client->demoDeltaNum == 0 ) {// First snapshot, don't delta compress.
        oldsnap = NULL;
        oldps = NULL;
    }
	else {
        oldsnap = &client->savedSnap;
        oldps = &client->savedSnap.ps;
    }
    
    MSG_WriteDeltaPlayerstate( &msg, oldps, &snap->ps );
    RS_EmitPacketEntities(oldsnap, snap, &msg, client->savedEnts);
    // Finalize message
    MSG_WriteByte(&msg, svc_EOF);
    
    // Write to demo file
    len = LittleLong(client->demoMessageSequence); // client->netchan.outgoingSequence
    FS_Write(&len, 4, client->demoFile);
    
    len = LittleLong(msg.cursize);
    FS_Write(&len, 4, client->demoFile);
    FS_Write(msg.data, msg.cursize, client->demoFile);
    
    // Save this snapshot for delta compression of next snapshot
    // Create deep copies of entity pointers for delta compression
    for (i = 0; i < snap->num_entities; i++) {
        // Make a copy of each entity state
        client->savedEnts[i] = *(snap->ents[i]);
    }
    // Copy the frame structure
    client->savedSnap = *snap;
    
    // Update tracking variables
    client->demoMessageSequence++;
    client->demoDeltaNum = 1; // Turn on delta for next snapshot
}

/*
=================
RS_EmitPacketEntities
=================
*/
static void RS_EmitPacketEntities( const clientSnapshot_t *from, const clientSnapshot_t *to, msg_t *msg, entityState_t *oldents ) {
	entityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->num_entities;
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;
	while ( newindex < to->num_entities || oldindex < from_num_entities ) {
		if ( newindex >= to->num_entities ) {
			newnum = MAX_GENTITIES+1;
		} else {
			newent = to->ents[ newindex ];
			newnum = newent->number;
		}

		if ( oldindex >= from_num_entities ) {
			oldnum = MAX_GENTITIES+1;
		} else {
			oldent = from->ents[ oldindex ];
			oldnum = oldent->number;
		}

		if ( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is qfalse, this will not result
			// in any bytes being emitted if the entity has not changed at all
			MSG_WriteDeltaEntity (msg, oldent, newent, qfalse );
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity (msg, &sv.svEntities[newnum].baseline, newent, qtrue );
			newindex++;
			continue;
		}

		if ( newnum > oldnum ) {
			// the old entity isn't present in the new message
			MSG_WriteDeltaEntity (msg, oldent, NULL, qtrue );
			oldindex++;
			continue;
		}
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );
}

void RS_DemoHandler(client_t *client) {
	clientSnapshot_t clFrame = client->frames[client->netchan.outgoingSequence & PACKET_MASK];
	int frameMsec = 1000 / sv_fps->integer * com_timescale->value;

    // Client un-recordable
    if (clFrame.ps.pm_type == PM_SPECTATOR || clFrame.ps.pm_type == PM_DEAD || clFrame.ps.pm_flags & PMF_FOLLOW) {
        if (client->awaitingDemoSave) { // short-circuit, save demo.
            RS_SaveDemo(client);
        }

        if (client->isRecording) {
            RS_StopRecord(client);
        }
    }

    // Player is recordable 
    else {        
        if (client->isRecording) { // Recording already
            RS_WriteSnapshot(client);
        }

        else { // Start recording
            if (client->state == CS_ACTIVE) {
                RS_StartRecord(client);
            }
        }
        
        // Save demo?
        if (client->awaitingDemoSave) {
            if (svs.time - client->timerStopTime > 500*frameMsec) // Enough frames have passed from timer stop to stop recording
                RS_SaveDemo(client);
        }
    }
}