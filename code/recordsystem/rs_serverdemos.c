#include "recordsystem.h"

// Static storage for delta compression
static clientSnapshot_t saved_snap;
static entityState_t saved_entity_states[MAX_SNAPSHOT_ENTITIES];
static entityState_t *saved_ents[MAX_SNAPSHOT_ENTITIES];

static void RS_EmitPacketEntities( const clientSnapshot_t *from, const clientSnapshot_t *to, msg_t *msg );
/*
====================
RS_StartRecord

Begins recording a demo for a given client
====================
*/
void RS_StartRecord(int clientNum, const char *plyrName, const char *str) {
    char demoName[MAX_OSPATH];
    client_t *cl;

    cl = &svs.clients[clientNum];
    if (sv.state != SS_GAME) {
        Com_Printf("Game must be running.\n");
        return;
    }

    if (cl->isRecording) {
        Com_Printf("Already recording client %i\n", clientNum);
        return;
    }

    // Create demo name
    Q_strncpyz(demoName, va("demos/[%s][%d].dm_68", sv_mapname->string, clientNum), sizeof(demoName));

    Com_Printf("recording to %s.\n", demoName);

    // open the demo file
    cl->demoFile = FS_FOpenFileWrite(demoName);
    if (cl->demoFile == FS_INVALID_HANDLE) {
        Com_Printf("ERROR: couldn't open file: %s.\n", demoName);
        return;
    }

    cl->isRecording = qtrue;
    cl->demoWaiting = qtrue;

	// write out the gamestate message
	RS_WriteGamestate( cl );

    // The gamestate will be written when the client is fully active
    // This happens in SV_SendClientSnapshot
}

/*
====================
RS_StopRecord

stop recording a demo
====================
*/
void RS_StopRecord(int clientNum, const char *plyrName, const char *str) {
    client_t *cl;
    cl = &svs.clients[clientNum];

    if (!cl->isRecording) {
        Com_Printf("Client %i is not being recorded\n", clientNum);
        return;
    }

    if (cl->demoFile != FS_INVALID_HANDLE) {
        int len;

        // Write proper EOF markers - TWO -1 values
        len = -1;
        FS_Write(&len, 4, cl->demoFile);
        FS_Write(&len, 4, cl->demoFile);
        
        FS_FCloseFile(cl->demoFile);
        cl->demoFile = FS_INVALID_HANDLE;
        Com_Printf("Stopped recording client %i\n", clientNum);
    }

    cl->isRecording = qfalse;
}

/*
====================
RS_WriteGamestate
====================
*/
void RS_WriteGamestate(client_t *client) {
	int			start;
	entityState_t nullstate;
	const svEntity_t *svEnt;
	msg_t		msg;
	byte		msgBuffer[ MAX_MSGLEN_BUF ];
    int len;
    // entityState_t *ent;
    // entityState_t nullstate;

	// accept usercmds starting from current server time only
	// Com_Memset( &client->lastUsercmd, 0x0, sizeof( client->lastUsercmd ) );
	// client->lastUsercmd.serverTime = sv.time - 1;

	MSG_Init( &msg, msgBuffer, MAX_MSGLEN );

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

    client->demoMessageSequence = 1;
	// send any server commands waiting to be sent first.
	// we have to do this cause we send the client->reliableSequence
	// with a gamestate and it sets the clc.serverCommandSequence at
	// the client side
	SV_UpdateServerCommandsToClient( client, &msg );

	// send the gamestate
	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, client->reliableSequence );

	// write the configstrings
	for ( start = 0 ; start < MAX_CONFIGSTRINGS ; start++ ) {
		if ( *sv.configstrings[ start ] != '\0' ) {
			MSG_WriteByte( &msg, svc_configstring );
			MSG_WriteShort( &msg, start );
			if ( start == CS_SYSTEMINFO && sv.pure != sv_pure->integer ) {
				// make sure we send latched sv.pure, not forced cvar value
				char systemInfo[BIG_INFO_STRING];
				Q_strncpyz( systemInfo, sv.configstrings[ start ], sizeof( systemInfo ) );
				Info_SetValueForKey_s( systemInfo, sizeof( systemInfo ), "sv_pure", va( "%i", sv.pure ) );
				MSG_WriteBigString( &msg, systemInfo );
			} else {
				MSG_WriteBigString( &msg, sv.configstrings[start] );
			}
		}
	}

	// write the baselines
	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( start = 0 ; start < MAX_GENTITIES; start++ ) {
		if ( !sv.baselineUsed[ start ] ) {
			continue;
		}
		svEnt = &sv.svEntities[ start ];
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, &svEnt->baseline, qtrue );
	}

	MSG_WriteByte( &msg, svc_EOF );

	MSG_WriteLong( &msg, client - svs.clients );

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

    // Write the client num - use actual client number
    MSG_WriteLong(&msg, client - svs.clients);

    // Write the checksum feed
    MSG_WriteLong(&msg, sv.checksumFeed);

    // End message
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
static void RS_WriteServerCommands(msg_t *msg, client_t *cl) {
    int i;

    // Write all commands since the last recorded command
    if (cl->reliableSequence - cl->demoCommandSequence > 0) {
        // Don't write more than MAX_RELIABLE_COMMANDS
        if (cl->reliableSequence - cl->demoCommandSequence > MAX_RELIABLE_COMMANDS) {
            cl->demoCommandSequence = cl->reliableSequence - MAX_RELIABLE_COMMANDS;
        }

        for (i = cl->demoCommandSequence + 1; i <= cl->reliableSequence; i++) {
            int index = i & (MAX_RELIABLE_COMMANDS - 1);
            MSG_WriteByte(msg, svc_serverCommand);
            MSG_WriteLong(msg, i);
            MSG_WriteString(msg, cl->reliableCommands[index]);
        }
    }

    cl->demoCommandSequence = cl->reliableSequence;
}

/*
====================
RS_WriteSnapshot
====================
*/
void RS_WriteSnapshot(client_t *cl) {
    byte bufData[MAX_MSGLEN_BUF];
    msg_t msg;
    int i, len;
    
    // Get current snapshot
    clientSnapshot_t *frame = &cl->frames[cl->netchan.outgoingSequence & PACKET_MASK];
    
    // Initialize message buffer
    MSG_Init(&msg, bufData, sizeof(bufData));
    MSG_Bitstream(&msg);
    
    // Write reliable sequence
    MSG_WriteLong(&msg, cl->reliableSequence);
    
    // Write server commands
    RS_WriteServerCommands(&msg, cl);
    
    // Write snapshot header
    MSG_WriteByte(&msg, svc_snapshot);
    MSG_WriteLong(&msg, sv.time);  // Server time
    MSG_WriteByte(&msg, cl->demoDeltaNum);  // 0 = no delta, 1 = delta
    MSG_WriteByte(&msg, 0);  // Snap flags
    
    // Write area info
    MSG_WriteByte(&msg, frame->areabytes);
    MSG_WriteData(&msg, frame->areabits, frame->areabytes);
    
    // Delta compress player state
    if (cl->demoDeltaNum == 0) {
        // First snapshot: no delta
        MSG_WriteDeltaPlayerstate(&msg, NULL, &frame->ps);
    } else {
        // Using previous snapshot for delta
        MSG_WriteDeltaPlayerstate(&msg, &saved_snap.ps, &frame->ps);
    }

    RS_EmitPacketEntities(&saved_snap, frame, &msg);
    // Finalize message
    MSG_WriteByte(&msg, svc_EOF);
    
    // Write to demo file
    len = LittleLong(cl->demoMessageSequence);
    FS_Write(&len, 4, cl->demoFile);
    
    len = LittleLong(msg.cursize);
    FS_Write(&len, 4, cl->demoFile);
    FS_Write(msg.data, msg.cursize, cl->demoFile);
    
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
    
    // Update tracking variables
    cl->demoMessageSequence++;
    cl->demoDeltaNum = 1;  // All future snapshots use delta
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

/*
====================
RS_CheckDemoFormat
====================
*/
void RS_CheckDemoFormat(client_t *cl) {
    // Verify we're not waiting for gamestate
    if (cl->demoWaiting) {
        Com_Printf("ERROR: Demo still waiting for gamestate!\n");
    }
    
    // Verify we have written at least one snapshot
    if (cl->demoMessageSequence <= 1) {
        Com_Printf("ERROR: No snapshots written yet!\n");
    }
    
    // Check the file status
    fileHandle_t f = cl->demoFile;
    if (f == FS_INVALID_HANDLE) {
        Com_Printf("ERROR: Invalid demo file handle!\n");
        return;
    }
    
    // Simply report that the demo file is valid
    Com_Printf("Demo file appears valid, written %d snapshots\n", cl->demoMessageSequence);
}

/*
====================
RS_WriteDemoMessage
====================
*/
void RS_WriteDemoMessage(client_t *cl, msg_t *msg) {
    if (!cl->isRecording) {
        return;
    }
    
    // Skip if waiting for first snapshot
    if (cl->demoWaiting) {
        if (cl->netchan.outgoingSequence > 0) {
            cl->demoWaiting = qfalse;
        } else {
            return;
        }
    }
    
    // Write the packet sequence
    int len = cl->netchan.outgoingSequence;
    int swlen = LittleLong(len);
    FS_Write(&swlen, 4, cl->demoFile);
    
    // Write the message size
    len = LittleLong(msg->cursize);
    FS_Write(&len, 4, cl->demoFile);
    
    // Write the message data
    FS_Write(msg->data, msg->cursize, cl->demoFile);
}

/*
====================
RS_RecordSnapshot
Hook function to be called from SV_SendClientSnapshot
====================
*/
void RS_RecordSnapshot(client_t *cl) {
    // If we're recording this client and they're active
    if (cl->isRecording && cl->state == CS_ACTIVE) {
        // If this is the first snapshot after connecting
        if (cl->demoWaiting) {
            cl->demoWaiting = qfalse;
            
            // Write the gamestate first
            RS_WriteGamestate(cl);
            
            Com_Printf("Demo recording started for client %i\n", (int)(cl - svs.clients));
        }
        
        // Now record this snapshot
        RS_WriteSnapshot(cl);
    }
}

#include <stdlib.h>
#include <string.h>

// Size of the message queue (must be power of 2)
#define DEMO_QUEUE_SIZE 1024

// Mask for queue indexing (DEMO_QUEUE_SIZE - 1)
#define DEMO_QUEUE_MASK (DEMO_QUEUE_SIZE - 1)

// Thread and queue handling
static struct {
    qboolean initialized;             // Is system initialized
    qboolean threadActive;            // Is thread running
    pthread_t threadId;               // Demo writer thread ID
    
    // Message queue
    demoQueuedMsg_t queue[DEMO_QUEUE_SIZE];  // Circular buffer of messages
    int queueHead;                    // Index to write new messages
    int queueTail;                    // Index to read next message
    int queueCount;                   // Current number of messages in queue
    
    // Synchronization
    pthread_mutex_t queueMutex;       // Mutex for thread safety
    pthread_cond_t dataAvailable;     // Condition: data available to process
    pthread_cond_t spaceAvailable;    // Condition: space available in queue
} demoThread;

// Function prototypes
static void* RS_DemoWriterThread(void *arg);
static qboolean RS_EnqueueMessage(demoQueuedMsg_t *msg);
static qboolean RS_DequeueMessage(demoQueuedMsg_t *msg);
static void RS_ProcessMessage(demoQueuedMsg_t *msg);

/*
====================
RS_InitThreadedDemos
====================
*/
qboolean RS_InitThreadedDemos(void) {
    // Check if already initialized
    if (demoThread.initialized) {
        Com_Printf("Threaded demo system already initialized\n");
        return qtrue;
    }
    
    // Initialize to zeros
    memset(&demoThread, 0, sizeof(demoThread));
    
    // Initialize mutex and condition variables
    if (pthread_mutex_init(&demoThread.queueMutex, NULL) != 0) {
        Com_Printf("Failed to initialize demo queue mutex\n");
        return qfalse;
    }
    
    if (pthread_cond_init(&demoThread.dataAvailable, NULL) != 0) {
        pthread_mutex_destroy(&demoThread.queueMutex);
        Com_Printf("Failed to initialize data available condition\n");
        return qfalse;
    }
    
    if (pthread_cond_init(&demoThread.spaceAvailable, NULL) != 0) {
        pthread_cond_destroy(&demoThread.dataAvailable);
        pthread_mutex_destroy(&demoThread.queueMutex);
        Com_Printf("Failed to initialize space available condition\n");
        return qfalse;
    }
    
    // Start the writer thread
    demoThread.threadActive = qtrue;
    if (pthread_create(&demoThread.threadId, NULL, RS_DemoWriterThread, NULL) != 0) {
        pthread_cond_destroy(&demoThread.spaceAvailable);
        pthread_cond_destroy(&demoThread.dataAvailable);
        pthread_mutex_destroy(&demoThread.queueMutex);
        Com_Printf("Failed to create demo writer thread\n");
        return qfalse;
    }
    
    demoThread.initialized = qtrue;
    Com_Printf("Threaded demo system initialized\n");
    return qtrue;
}

/*
====================
RS_ShutdownThreadedDemos
====================
*/
void RS_ShutdownThreadedDemos(void) {
    demoQueuedMsg_t msg;
    
    if (!demoThread.initialized) {
        return;
    }
    
    // Signal thread to stop
    pthread_mutex_lock(&demoThread.queueMutex);
    demoThread.threadActive = qfalse;
    pthread_cond_signal(&demoThread.dataAvailable);
    pthread_mutex_unlock(&demoThread.queueMutex);
    
    // Wait for thread to finish
    pthread_join(demoThread.threadId, NULL);
    
    // Clean up any remaining messages in the queue
    while (RS_DequeueMessage(&msg)) {
        if (msg.data) {
            Z_Free(msg.data);
        }
    }
    
    // Destroy synchronization objects
    pthread_cond_destroy(&demoThread.spaceAvailable);
    pthread_cond_destroy(&demoThread.dataAvailable);
    pthread_mutex_destroy(&demoThread.queueMutex);
    
    demoThread.initialized = qfalse;
    Com_Printf("Threaded demo system shut down\n");
}

/*
====================
RS_StartThreadedRecord
====================
*/
void RS_StartThreadedRecord(int clientNum, const char *plyrName, const char *demoName) {
    if (!demoThread.initialized) {
        Com_Printf("Threaded demo system not initialized\n");
        return;
    }
    
    // Let the original function handle the setup
    RS_StartRecord(clientNum, plyrName, demoName);
    
    // The gamestate will be queued when the client is fully active
    // This happens in SV_SendClientSnapshot via RS_QueueGamestate
}

/*
====================
RS_StopThreadedRecord
====================
*/
void RS_StopThreadedRecord(int clientNum, const char *plyrName, const char *str) {
    demoQueuedMsg_t msg;
    client_t *cl;
    
    if (!demoThread.initialized) {
        Com_Printf("Threaded demo system not initialized\n");
        return;
    }
    
    cl = &svs.clients[clientNum];
    if (!cl->isRecording) {
        Com_Printf("Client %i is not being recorded\n", clientNum);
        return;
    }
    
    // Send an end recording message to the thread
    msg.type = DEMO_MSG_END_RECORDING;
    msg.clientNum = clientNum;
    msg.sequence = 0;
    msg.serverTime = sv.time;
    msg.data = NULL;
    msg.dataSize = 0;
    
    if (!RS_EnqueueMessage(&msg)) {
        Com_Printf("Failed to queue stop recording message\n");
        // Fall back to direct stop
        RS_StopRecord(clientNum, plyrName, str);
        return;
    }
    
    // Mark client as not recording to prevent new messages
    cl->isRecording = qfalse;
    
    Com_Printf("Queued stop recording for client %i\n", clientNum);
}

/*
====================
RS_QueueGamestate
====================
*/
void RS_QueueGamestate(client_t *client) {
    demoQueuedMsg_t msg;
    byte *buffer;
    msg_t qmsg;
    byte msgBuffer[MAX_MSGLEN_BUF];
    int i;
    entityState_t nullstate;
    const svEntity_t *svEnt;
    
    if (!demoThread.initialized || !client->isRecording) {
        return;
    }
    
    // Initialize message
    MSG_Init(&qmsg, msgBuffer, sizeof(msgBuffer));
    
    // Write the message just like RS_WriteGamestate does
    MSG_WriteLong(&qmsg, client->lastClientCommand);
    
    // Send any server commands
    SV_UpdateServerCommandsToClient(client, &qmsg);
    
    // Send the gamestate
    MSG_WriteByte(&qmsg, svc_gamestate);
    MSG_WriteLong(&qmsg, client->reliableSequence);
    
    // Write the configstrings
    for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
        if (*sv.configstrings[i] != '\0') {
            MSG_WriteByte(&qmsg, svc_configstring);
            MSG_WriteShort(&qmsg, i);
            if (i == CS_SYSTEMINFO && sv.pure != sv_pure->integer) {
                // Special handling for system info
                char systemInfo[BIG_INFO_STRING];
                Q_strncpyz(systemInfo, sv.configstrings[i], sizeof(systemInfo));
                Info_SetValueForKey_s(systemInfo, sizeof(systemInfo), "sv_pure", va("%i", sv.pure));
                MSG_WriteBigString(&qmsg, systemInfo);
            } else {
                MSG_WriteBigString(&qmsg, sv.configstrings[i]);
            }
        }
    }
    
    // Write the baselines
    Com_Memset(&nullstate, 0, sizeof(nullstate));
    for (i = 0; i < MAX_GENTITIES; i++) {
        if (!sv.baselineUsed[i]) {
            continue;
        }
        svEnt = &sv.svEntities[i];
        MSG_WriteByte(&qmsg, svc_baseline);
        MSG_WriteDeltaEntity(&qmsg, &nullstate, &svEnt->baseline, qtrue);
    }
    
    MSG_WriteByte(&qmsg, svc_EOF);
    MSG_WriteLong(&qmsg, client - svs.clients);
    MSG_WriteLong(&qmsg, sv.checksumFeed);
    MSG_WriteByte(&qmsg, svc_EOF);
    
    // Copy the message data
    buffer = Z_Malloc(qmsg.cursize);
    memcpy(buffer, qmsg.data, qmsg.cursize);
    
    // Setup the message for the queue
    msg.type = DEMO_MSG_GAMESTATE;
    msg.clientNum = client - svs.clients;
    msg.sequence = 0;  // Gamestate uses sequence 0
    msg.serverTime = sv.time;
    msg.data = buffer;
    msg.dataSize = qmsg.cursize;
    
    // Queue the message
    if (!RS_EnqueueMessage(&msg)) {
        Com_Printf("Failed to queue gamestate, queue full\n");
        Z_Free(buffer);
        return;
    }
    
    // Reset client's demo sequence tracking
    client->demoMessageSequence = 1;
    client->demoDeltaNum = 0;
    
    Com_Printf("Queued gamestate for client %i\n", msg.clientNum);
}

/*
====================
RS_QueueSnapshot
====================
*/
void RS_QueueSnapshot(client_t *client) {
    demoQueuedMsg_t msg;
    byte *buffer;
    msg_t qmsg;
    byte msgBuffer[MAX_MSGLEN_BUF];
    
    if (!demoThread.initialized || !client->isRecording) {
        return;
    }
    
    // Let the existing code prepare the snapshot message
    MSG_Init(&qmsg, msgBuffer, sizeof(msgBuffer));
    MSG_Bitstream(&qmsg);
    
    // Write reliable sequence
    MSG_WriteLong(&qmsg, client->reliableSequence);
    
    // Write server commands
    if (client->reliableSequence - client->demoCommandSequence > 0) {
        int i;
        
        // Don't write more than MAX_RELIABLE_COMMANDS
        if (client->reliableSequence - client->demoCommandSequence > MAX_RELIABLE_COMMANDS) {
            client->demoCommandSequence = client->reliableSequence - MAX_RELIABLE_COMMANDS;
        }
        
        for (i = client->demoCommandSequence + 1; i <= client->reliableSequence; i++) {
            int index = i & (MAX_RELIABLE_COMMANDS - 1);
            MSG_WriteByte(&qmsg, svc_serverCommand);
            MSG_WriteLong(&qmsg, i);
            MSG_WriteString(&qmsg, client->reliableCommands[index]);
        }
    }
    
    client->demoCommandSequence = client->reliableSequence;
    
    // Get current snapshot
    clientSnapshot_t *frame = &client->frames[client->netchan.outgoingSequence & PACKET_MASK];
    
    // Write snapshot header
    MSG_WriteByte(&qmsg, svc_snapshot);
    MSG_WriteLong(&qmsg, sv.time);
    MSG_WriteByte(&qmsg, client->demoDeltaNum);
    MSG_WriteByte(&qmsg, 0);  // Snap flags
    
    // Write area info
    MSG_WriteByte(&qmsg, frame->areabytes);
    MSG_WriteData(&qmsg, frame->areabits, frame->areabytes);
    
    // Delta compress player state
    if (client->demoDeltaNum == 0) {
        // First snapshot: no delta
        MSG_WriteDeltaPlayerstate(&qmsg, NULL, &frame->ps);
    } else {
        // Using previous snapshot for delta - this will be handled in RS_WriteSnapshot
        // We just prepare the basic message here
        MSG_WriteDeltaPlayerstate(&qmsg, NULL, &frame->ps);
    }
    
    // Don't attempt to delta compress entities here
    // That's better handled in RS_WriteSnapshot directly
    // Just send a placeholder entity message indicating we need to do it there
    MSG_WriteByte(&qmsg, svc_EOF);
    
    // Copy the message data
    buffer = Z_Malloc(qmsg.cursize);
    memcpy(buffer, qmsg.data, qmsg.cursize);
    
    // Setup the message for the queue
    msg.type = DEMO_MSG_SNAPSHOT;
    msg.clientNum = client - svs.clients;
    msg.sequence = client->netchan.outgoingSequence;
    msg.serverTime = sv.time;
    msg.data = buffer;
    msg.dataSize = qmsg.cursize;
    
    // Queue the message
    if (!RS_EnqueueMessage(&msg)) {
        Com_Printf("Failed to queue snapshot, queue full\n");
        Z_Free(buffer);
        return;
    }
}

/*
====================
RS_EnqueueMessage
====================
*/
static qboolean RS_EnqueueMessage(demoQueuedMsg_t *msg) {
    qboolean result = qfalse;
    
    pthread_mutex_lock(&demoThread.queueMutex);
    
    // Wait if queue is full (with timeout)
    while (demoThread.queueCount >= DEMO_QUEUE_SIZE && demoThread.threadActive) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;  // 1 second timeout
        
        // Wait for space to become available
        pthread_cond_timedwait(&demoThread.spaceAvailable, &demoThread.queueMutex, &ts);
    }
    
    // Check if we can add to the queue
    if (demoThread.queueCount < DEMO_QUEUE_SIZE && demoThread.threadActive) {
        // Copy message to the queue
        demoThread.queue[demoThread.queueHead] = *msg;
        
        // Update queue pointers
        demoThread.queueHead = (demoThread.queueHead + 1) & DEMO_QUEUE_MASK;
        demoThread.queueCount++;
        
        // Signal that data is available
        pthread_cond_signal(&demoThread.dataAvailable);
        
        result = qtrue;
    }
    
    pthread_mutex_unlock(&demoThread.queueMutex);
    return result;
}

/*
====================
RS_DequeueMessage
====================
*/
static qboolean RS_DequeueMessage(demoQueuedMsg_t *msg) {
    qboolean result = qfalse;
    
    pthread_mutex_lock(&demoThread.queueMutex);
    
    // Wait for data if queue is empty
    while (demoThread.queueCount == 0 && demoThread.threadActive) {
        // Wait for data to become available
        pthread_cond_wait(&demoThread.dataAvailable, &demoThread.queueMutex);
    }
    
    // Check if we have data
    if (demoThread.queueCount > 0) {
        // Copy message from the queue
        *msg = demoThread.queue[demoThread.queueTail];
        
        // Update queue pointers
        demoThread.queueTail = (demoThread.queueTail + 1) & DEMO_QUEUE_MASK;
        demoThread.queueCount--;
        
        // Signal that space is available
        pthread_cond_signal(&demoThread.spaceAvailable);
        
        result = qtrue;
    }
    
    pthread_mutex_unlock(&demoThread.queueMutex);
    return result;
}

/*
====================
RS_ProcessMessage
====================
*/
static void RS_ProcessMessage(demoQueuedMsg_t *msg) {
    client_t *client;
    int len;
    
    // Get the client
    if (msg->clientNum < 0 || msg->clientNum >= sv_maxclients->integer) {
        Com_Printf("Invalid client number %d in demo message\n", msg->clientNum);
        return;
    }
    
    client = &svs.clients[msg->clientNum];
    
    // Process based on message type
    switch (msg->type) {
        case DEMO_MSG_GAMESTATE:
            if (client->demoFile != FS_INVALID_HANDLE) {
                // Write sequence (0 for gamestate)
                len = LittleLong(0);
                FS_Write(&len, 4, client->demoFile);
                
                // Write message size
                len = LittleLong(msg->dataSize);
                FS_Write(&len, 4, client->demoFile);
                
                // Write message data
                FS_Write(msg->data, msg->dataSize, client->demoFile);
                
                Com_Printf("Wrote gamestate for client %i\n", msg->clientNum);
            }
            break;
            
        case DEMO_MSG_SNAPSHOT:
            if (client->demoFile != FS_INVALID_HANDLE) {
                // Let the existing code write the snapshot
                // This handles delta compression correctly
                RS_WriteSnapshot(client);
                
                Com_Printf("Wrote snapshot %i for client %i\n", 
                          client->demoMessageSequence - 1, msg->clientNum);
            }
            break;
            
        case DEMO_MSG_END_RECORDING:
            // Close the demo file
            if (client->demoFile != FS_INVALID_HANDLE) {
                int len = -1;
                
                // Write proper EOF markers - TWO -1 values
                FS_Write(&len, 4, client->demoFile);
                FS_Write(&len, 4, client->demoFile);
                
                FS_FCloseFile(client->demoFile);
                client->demoFile = FS_INVALID_HANDLE;
                
                Com_Printf("Finished recording demo for client %i\n", msg->clientNum);
            }
            break;
            
        default:
            Com_Printf("Unknown demo message type %d\n", msg->type);
            break;
    }
}

/*
====================
RS_DemoWriterThread
====================
*/
static void* RS_DemoWriterThread(void *arg) {
    demoQueuedMsg_t msg;
    
    Com_Printf("Demo writer thread started\n");
    
    while (demoThread.threadActive) {
        // Wait for a message
        if (RS_DequeueMessage(&msg)) {
            // Process the message
            RS_ProcessMessage(&msg);
            
            // Free the message data
            if (msg.data) {
                Z_Free(msg.data);
            }
        }
    }
    
    Com_Printf("Demo writer thread stopped\n");
    return NULL;
}

/*
====================
RS_GetQueuedMessageCount
====================
*/
int RS_GetQueuedMessageCount(void) {
    int count;
    
    pthread_mutex_lock(&demoThread.queueMutex);
    count = demoThread.queueCount;
    pthread_mutex_unlock(&demoThread.queueMutex);
    
    return count;
}