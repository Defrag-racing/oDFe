#include "../server/server.h"


static void RS_EmitPacketEntities( const clientSnapshot_t *from, const clientSnapshot_t *to, msg_t *msg );

RS_WriteMessageToDemo(clientDemo_t *demo) {

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
static void RS_WriteSnapshot(clientDemo_t *demo) {
    client_t* client = &svs.clients[demo->clientNum];
    clientSnapshot_t saved_snap;
    entityState_t saved_entity_states[MAX_SNAPSHOT_ENTITIES];
    entityState_t *saved_ents[MAX_SNAPSHOT_ENTITIES];
    clientSnapshot_t *frame = demo->currSnap;


    byte bufData[MAX_MSGLEN_BUF];
    msg_t msg;
    int i, len;
    
    // Get current snapshot
    // clientSnapshot_t *frame = &client->frames[client->netchan.outgoingSequence & PACKET_MASK];
    // RS_Log("", "DEBUG");
    
    // Initialize message buffer
    MSG_Init(&msg, bufData, sizeof(bufData));
    MSG_Bitstream(&msg);
    
    // Write reliable sequence
    MSG_WriteLong(&msg, frame->reliableSequence);
    
    // Write server commands
    RS_WriteServerCommands(&msg, client);
    
    // Write snapshot header
    MSG_WriteByte(&msg, svc_snapshot);
    MSG_WriteLong(&msg, frame->time);  // Server time
    MSG_WriteByte(&msg, demo->deltaNum);  // 0 = no delta, 1 = delta
    MSG_WriteByte(&msg, 0);  // Snap flags
    
    // Write area info
    MSG_WriteByte(&msg, frame->areabytes);
    MSG_WriteData(&msg, frame->areabits, frame->areabytes);
    
    // Delta compress player state ?
    if (!demo->deltaNum) {
        // First snapshot: no delta
        MSG_WriteDeltaPlayerstate(&msg, NULL, &frame->ps);
    } else {
        // Using previous snapshot for delta
        MSG_WriteDeltaPlayerstate(&msg, &demo->prevSnap, &frame->ps);
    }

    RS_EmitPacketEntities(demo->prevSnap, frame, &msg);
    // Finalize message
    MSG_WriteByte(&msg, svc_EOF);
    
    // Write to demo file
    len = LittleLong(frame->frameNum);
    FS_Write(&len, 4, demo->file);
    
    len = LittleLong(msg.cursize);
    FS_Write(&len, 4, demo->file);
    FS_Write(msg.data, msg.cursize, demo->file);
    
    // Save this snapshot for delta compression of next snapshot
    // Create deep copies of entity pointers for delta compression
    for (i = 0; i < frame->num_entities; i++) {
        if (frame->ents[i]) {
            // Make a copy of each entity state
            saved_entity_states[i] = *(frame->ents[i]);
            saved_ents[i] = &saved_entity_states[i];
        } else {
            saved_ents[i] = NULL;
        }
    }

    // Copy the frame structure
    saved_snap = *frame;
    
    // Update the entity pointers to our saved copies
    for (i = 0; i < MAX_SNAPSHOT_ENTITIES; i++) {
        saved_snap.ents[i] = saved_ents[i];
    }
    
    demo->prevSnap = saved_snap;
    // Update tracking variables
    demo->currSeq++;
    demo->deltaNum = 1;  // All future snapshots use delta
}

/*
=================
RS_EmitPacketEntities
=================
*/
static void RS_EmitPacketEntities( const clientSnapshot_t *from, const clientSnapshot_t *to, msg_t *msg ) {
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

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}

void RS_MarkSnapshot(clientSnapshot_t *clFrame, client_t *client) {
	int frameMsec = 1000 / sv_fps->integer * com_timescale->value;

    // Client is un-recordable
    if (clFrame->ps.pm_type == PM_SPECTATOR || clFrame->ps.pm_type == PM_DEAD || clFrame->ps.pm_flags & PMF_FOLLOW) {
        if (client->awaitingDemoSave) { // short-circuit, save demo.
            clFrame->serverDemoSave = qtrue;
            clFrame->timeInfo = client->timeInfo;
            client->awaitingDemoSave = qfalse;
        }
        
        if (client->recording) {
            clFrame->clientDemoEnd = qtrue; // Mark end of a client demo
            client->recording = qfalse;
        }
    }

    // Client is recordable 
    else {        
        if (!client->awaitingDemoStart && client->state == CS_ACTIVE) {
            clFrame->clientDemoStart = qtrue; // Mark start of a demo
            // Add data needed for gamestate
            clFrame->last =
            client->awaitingDemoStart = qfalse; // Stop waiting for demo start
        }

        // Save demo?
        if (client->awaitingDemoSave) {
            if (svs.time - client->timerStopTime > 500*frameMsec) // Enough frames have passed from timer stop to stop recording
                clFrame->serverDemoSave = qtrue; // Mark frame to a save
                clFrame->timeInfo = client->timeInfo; // Add timer info for demo thread
                client->awaitingDemoSave = qfalse; // Stop waiting for demo save
        }
    }
}

/*
====================
RS_StartClientDemo

Begins recording a demo for a given client
====================
*/
static qboolean RS_StartClientDemo(clientDemo_t *demo) {
    char *demoName;
    client_t* client = &svs.clients[demo->clientNum];

    Q_strncpyz(demoName, va("demos/[%d].dm_68", demo->clientNum), sizeof(demoName));
    
    Com_Printf("recording to %s.\n", demoName);

    // Start writing to demo file
    demo->file = FS_FOpenFileWrite(demoName);
    if (demo->file == FS_INVALID_HANDLE) {
        Com_Printf("ERROR: couldn't open file: %s.\n", demoName);
        return qfalse;
    }

    Q_strncpyz(client->demoName, demoName, sizeof(demoName));

	// write out the gamestate message
    FS_Write(demo->gamestateMsg, demo->gamestateMsg.cursize, demo->file);
    demo->deltaNum = 0; // don't use delta for first snapshot
    demo->isActive = qtrue;
    return qtrue;
}

/*
====================
RS_SaveServerDemo

Save a server demo
====================
*/

static qboolean RS_SaveServerDemo(clientDemo_t *demo, timeInfo_t *timeInfo) {
    char finalName[MAX_OSPATH];
    client_t* client = &svs.clients[demo->clientNum];
    
    if (!demo->isActive) {
        Com_Printf("Attempted a save for an inactive client demo. Client %i is not being recorded\n", demo->clientNum);
        return qfalse;
    }

    if (demo->file != FS_INVALID_HANDLE) {
        int len = -1;

        FS_Write(&len, 4, demo->file);
        FS_Write(&len, 4, demo->file);
        
        FS_FCloseFile(demo->file);
        demo->file = FS_INVALID_HANDLE;
        Com_Printf("Stopped recording client %i\n", demo->clientNum);
    }

    if (timeInfo->gametype == 1) { // run mode
        Com_sprintf( finalName, sizeof( finalName ), "demos/%s[df.%s]%s[%s][%s].dm_68", \
        timeInfo->mapname, \
        timeInfo->promode ? "cpm" : "vq3", \
        formatTime(timeInfo->time), \
        client->displayName, \
        client->uuid);
    }
    else {
        Com_sprintf( finalName, sizeof( finalName ), "demos/%s[df.%s.%i]%s[%s][%s].dm_68", \
        timeInfo->mapname, \
        timeInfo->promode ? "cpm" : "vq3", \
        timeInfo->submode, \
        formatTime(timeInfo->time), \
        client->displayName, \
        client->uuid);
    }

    if (demo->file != FS_INVALID_HANDLE) {
        int len = -1;

        FS_Write(&len, 4, demo->file);
        FS_Write(&len, 4, demo->file);
        
        FS_FCloseFile(demo->file);
        demo->file = FS_INVALID_HANDLE;
        Com_Printf("RS_INFO: Stopped recording client %i\n", demo->clientNum);
    }

    FS_Rename( client->demoName, finalName );
    Com_Printf("RS_INFO: Saved demo: %s\n", finalName);
    demo->file = FS_INVALID_HANDLE;
    demo->isActive = qfalse;
    demo->deltaNum = 0;
    return qtrue;
}

/*
====================
RS_StopClientDemo

stop recording a client demo
====================
*/
static qboolean RS_StopClientDemo(clientDemo_t *demo) {
    client_t *client = &svs.clients[demo->clientNum];

    if (!demo->isActive) {
        Com_Printf("RS_WARNING: Attempted stop of inactive client demo for client %i.\n", demo->clientNum);
        return qfalse;
    }

    if (demo->file != FS_INVALID_HANDLE) {
        int len;

        // Write proper EOF markers - TWO -1 values
        len = -1;
        FS_Write(&len, 4, demo->file);
        FS_Write(&len, 4, demo->file);
        
        FS_FCloseFile(demo->file);
        demo->file = FS_INVALID_HANDLE;
        Com_Printf("RS_INFO: Stopped recording client %i\n", demo->clientNum);
    }

    client->isRecording = qfalse;
    // client->demoWaiting = qfalse;
    client->demoDeltaNum = 0;
    return qtrue;
}

void* RS_DemoWriterThread() {
    int currSeq;
    int lastWrittenSeq;

    // TODO: Add CVAR:
    // while (!sv.serverdemos) {
    while (qtrue) {
        for (int i = 0; i < sv_maxclients->integer; i++) {
            client_t *client = &svs.clients[i];
            clientDemo_t *clientdemo = &clientDemos[i];
            
            // Activate demo if the client is active
            if (!clientdemo->isActive && client->state == CS_ACTIVE) {
                RS_StartClientDemo(clientdemo);
                clientdemo->isActive = qtrue;
            }
            
            // Client demo not active, skip to the next client demo
            if (!clientdemo->isActive) {
                continue;
            }

            if (clientdemo->lastWrittenSeq) {
                currSeq = clientdemo->lastWrittenSeq + 1;
            }
            else {
                currSeq = clientdemo->startSeq;
                lastWrittenSeq = clientdemo->startSeq;
            }
            
            const clientSnapshot_t *snapshot;
            memcpy(&client->currSnap, client->frames[currSeq & PACKET_MASK], sizeof(clientSnapshot_t));
            
            if (snapshot->clientDemoStart) {
                RS_StartClientDemo(clientdemo);
            }
            else if (snapshot->clientDemoEnd) {
                RS_StopClientDemo(clientdemo);
            }
            else if (snapshot->serverDemoSave) {
                RS_SaveServerDemo(clientdemo, snapshot->timeInfo);
            }
            else
                RS_WriteSnapshot(clientdemo);

            free(snapshot);
        }
    }
}


/*
====================
CL_WriteServerCommands
====================
*/
static void CL_WriteServerCommands( msg_t *msg ) {
	int i;

	if ( clc.serverCommandSequence - clc.demoCommandSequence > 0 ) {

		// do not write more than MAX_RELIABLE_COMMANDS
		if ( clc.serverCommandSequence - clc.demoCommandSequence > MAX_RELIABLE_COMMANDS ) {
			clc.demoCommandSequence = clc.serverCommandSequence - MAX_RELIABLE_COMMANDS;
		}

		for ( i = clc.demoCommandSequence + 1 ; i <= clc.serverCommandSequence; i++ ) {
			MSG_WriteByte( msg, svc_serverCommand );
			MSG_WriteLong( msg, i );
			MSG_WriteString( msg, clc.serverCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
		}
	}

	clc.demoCommandSequence = clc.serverCommandSequence;
}


/*
=============
CL_EmitPacketEntities
=============
*/
static void CL_EmitPacketEntities( clSnapshot_t *from, clSnapshot_t *to, msg_t *msg, entityState_t *oldents ) {
	entityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->numEntities;
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;
	while ( newindex < to->numEntities || oldindex < from_num_entities ) {
		if ( newindex >= to->numEntities ) {
			newnum = MAX_GENTITIES+1;
		} else {
			newent = &cl.parseEntities[(to->parseEntitiesNum + newindex) % MAX_PARSE_ENTITIES];
			newnum = newent->number;
		}

		if ( oldindex >= from_num_entities ) {
			oldnum = MAX_GENTITIES+1;
		} else {
			//oldent = &cl.parseEntities[(from->parseEntitiesNum + oldindex) % MAX_PARSE_ENTITIES];
			oldent = &oldents[ oldindex ];
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
			MSG_WriteDeltaEntity (msg, &cl.entityBaselines[newnum], newent, qtrue );
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

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}


/*
====================
CL_WriteSnapshot
====================
*/
static void CL_WriteSnapshot( void ) {

	static	clSnapshot_t saved_snap;
	static entityState_t saved_ents[ MAX_SNAPSHOT_ENTITIES ];

	clSnapshot_t *snap, *oldSnap;
	byte	bufData[ MAX_MSGLEN_BUF ];
	msg_t	msg;
	int		i, len;

	snap = &cl.snapshots[ cl.snap.messageNum & PACKET_MASK ]; // current snapshot
	//if ( !snap->valid ) // should never happen?
	//	return;

	if ( clc.demoDeltaNum == 0 ) {
		oldSnap = NULL;
	} else {
		oldSnap = demo->prevSnap;
	}

	MSG_Init( &msg, bufData, MAX_MSGLEN );
	MSG_Bitstream( &msg );

	// NOTE, MRE: all server->client messages now acknowledge
	MSG_WriteLong( &msg, clc.reliableSequence );

	// Write all pending server commands
	CL_WriteServerCommands( &msg );

	MSG_WriteByte( &msg, svc_snapshot );
	MSG_WriteLong( &msg, snap->serverTime ); // sv.time
	MSG_WriteByte( &msg, clc.demoDeltaNum ); // 0 or 1
	MSG_WriteByte( &msg, snap->snapFlags );  // snapFlags
	MSG_WriteByte( &msg, snap->areabytes );  // areabytes
	MSG_WriteData( &msg, snap->areamask, snap->areabytes );
	if ( oldSnap )
		MSG_WriteDeltaPlayerstate( &msg, &oldSnap->ps, &snap->ps );
	else
		MSG_WriteDeltaPlayerstate( &msg, NULL, &snap->ps );

	CL_EmitPacketEntities( oldSnap, snap, &msg, saved_ents );

	// finished writing the client packet
	MSG_WriteByte( &msg, svc_EOF );

	// write it to the demo file
	if ( clc.demoplaying )
		len = LittleLong( clc.demoMessageSequence );
	else
		len = LittleLong( clc.serverMessageSequence );
	FS_Write( &len, 4, clc.recordfile );

	len = LittleLong( msg.cursize );
	FS_Write( &len, 4, clc.recordfile );
	FS_Write( msg.data, msg.cursize, clc.recordfile );

	// save last sent state so if there any need - we can skip any further incoming messages
	for ( i = 0; i < snap->numEntities; i++ )
		saved_ents[ i ] = cl.parseEntities[ (snap->parseEntitiesNum + i) % MAX_PARSE_ENTITIES ];

	saved_snap = *snap;
	saved_snap.parseEntitiesNum = 0;

	clc.demoMessageSequence++;
	clc.demoDeltaNum = 1;
}