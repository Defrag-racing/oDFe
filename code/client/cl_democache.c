/*
===========================================================================
Demo keyframe cache (launcher demo player, smooth bidirectional scrubbing).

A Quake3 demo is a forward-only, delta-compressed stream: a snapshot is
decoded relative to the previous one, so there is no random access. To seek
backward the engine would otherwise have to restart the demo and replay from
the gamestate (reloading the map) - which is what made backward seeking jump
and "reload".

This module removes that: while the demo plays forward, every few seconds of
demo time we snapshot the FULL client state (clientActive_t + clientConnection_t
+ the demo file offset) into memory. To seek backward we restore the nearest
keyframe at-or-before the target, give the cgame a clean re-init from that
restored state, and let the normal read-ahead fast-forward the small remainder
(< one keyframe interval).

Why re-init the cgame instead of just feeding it older snapshots? The (compiled,
uneditable) cgame VM tracks the highest snapshot number AND serverTime it has
seen and rejects anything "older" (CG_ProcessSnapshots errors: "n <
latestSnapshotNum" / "server time went backwards"). DeFRaG additionally
checksums the snapshot stream, so faking monotonic numbers/times corrupts the
run timer ("checksum failed"). A clean CL_ShutdownCGame + CL_InitCGame resets
all of that, and because CM_LoadMap (already cached) and RE_LoadWorldMap (now
skips a same-map reload) are no-ops for the map that's still loaded, the re-init
has NO map-reload hitch. So seeking is correct AND has the real demo time.

clientActive_t is wiped wholesale at every gamestate (it's plain old data, no
pointers), so a memcpy captures all delta-reference state (snap, the snapshots
ring, parseEntities, baselines, configstrings). clientConnection_t is copied
too (serverMessageSequence / lastExecutedServerCommand / clientNum that CG_INIT
needs); the demo file handle is constant for the demo's lifetime so it survives
the copy (we re-seek the file by offset).

Memory: ~sizeof(clientActive_t)+sizeof(clientConnection_t) (~2 MB) per
keyframe, so cost = demoLength/interval. The interval is cl_demoKeyframeMsec
(default 5s): a 3-minute demo is ~80 MB, tunable up for very long demos.
===========================================================================
*/

#include "client.h"
#include <stdlib.h>

typedef struct {
	int						time;		// cl.snap.serverTime captured here (real demo time)
	int						offset;		// FS_FTell position to resume reading after
	clientActive_t			cl;
	clientConnection_t		clc;
} demoKeyframe_t;

// Memory budget for the keyframe cache. This bounds RAM no matter how long the
// demo is: when the cache fills we decimate (drop every other keyframe) and
// double the spacing, so a 10-minute demo keeps ~5 s granularity while a
// multi-hour demo simply gets coarser keyframes (a longer but still bounded
// fast-forward after a seek). Memory never grows unbounded.
#define KF_BUDGET_BYTES ( 384u << 20 )	// ~384 MB

static demoKeyframe_t	*kf = NULL;
static int				kfCount = 0;
static int				kfAlloc = 0;
static int				kfSpacing = 0;	// current ms between keyframes (grows on decimation)
static cvar_t			*cl_demoKeyframeMsec = NULL;

// Max keyframes the budget allows (>= 32 so short demos always cache fully).
static int CL_DemoCacheMax( void ) {
	int n = (int)( KF_BUDGET_BYTES / sizeof( demoKeyframe_t ) );
	return n < 32 ? 32 : n;
}

void CL_DemoCacheReset( void ) {
	kfCount = 0;	// keep the allocation, just forget the keyframes
	kfSpacing = 0;	// re-derived from the cvar on the next capture
}

void CL_DemoCacheFree( void ) {
	if ( kf ) {
		free( kf );
		kf = NULL;
	}
	kfCount = 0;
	kfAlloc = 0;
}

static int CL_DemoKeyframeInterval( void ) {
	int v;
	if ( !cl_demoKeyframeMsec ) {
		cl_demoKeyframeMsec = Cvar_Get( "cl_demoKeyframeMsec", "5000", CVAR_ARCHIVE_ND );
		Cvar_SetDescription( cl_demoKeyframeMsec,
			"Demo player: seconds*1000 between cached keyframes; lower = smoother backward seek but more memory." );
	}
	v = cl_demoKeyframeMsec->integer;
	if ( v < 500 ) v = 500;
	return v;
}

/*
==================
CL_DemoCacheTrack

Called after each demo message is read. Appends a keyframe at the playback
frontier (a new maximum serverTime) once per interval. Re-reading already
cached territory (e.g. the fast-forward after a restore) does not re-capture,
because its serverTime is below the last keyframe.
==================
*/
void CL_DemoCacheTrack( void ) {
	if ( !cl_demoPlayer || !clc.demoplaying || !cl.snap.valid ) {
		return;
	}
	if ( cl_demoStartTime == 0 ) {
		cl_demoStartTime = cl.snap.serverTime;
	}
	if ( kfSpacing == 0 ) {
		kfSpacing = CL_DemoKeyframeInterval();
	}

	// only at the frontier, spaced by the interval (always grab the first one)
	if ( kfCount > 0 && cl.snap.serverTime < kf[kfCount - 1].time + kfSpacing ) {
		return;
	}

	// cache full: keep every other keyframe (index 0 stays) and double the
	// spacing, so memory stays bounded for arbitrarily long demos.
	if ( kfCount >= CL_DemoCacheMax() ) {
		int i, n = 0;
		for ( i = 0; i < kfCount; i += 2 ) {
			kf[n++] = kf[i];
		}
		kfCount = n;
		kfSpacing *= 2;
		// the just-read frame may now be within the widened spacing -> skip it
		if ( cl.snap.serverTime < kf[kfCount - 1].time + kfSpacing ) {
			return;
		}
	}

	if ( kfCount >= kfAlloc ) {
		int cap = CL_DemoCacheMax();
		int na = kfAlloc ? kfAlloc * 2 : 64;
		if ( na > cap ) na = cap;
		if ( na > kfAlloc ) {
			demoKeyframe_t *nk = (demoKeyframe_t *)realloc( kf, (size_t)na * sizeof( demoKeyframe_t ) );
			if ( !nk ) {
				return; // out of memory: stop caching, seeking still works (coarser)
			}
			kf = nk;
			kfAlloc = na;
		}
	}

	kf[kfCount].time = cl.snap.serverTime;
	kf[kfCount].offset = FS_FTell( clc.demofile );
	kf[kfCount].cl = cl;
	kf[kfCount].clc = clc;
	kfCount++;
}

// Nearest keyframe index with time <= targetTime, or -1 if none. Keyframes are
// appended in increasing demo time, so the array is sorted.
static int CL_DemoCacheFind( int targetTime ) {
	int i, best = -1;
	for ( i = 0; i < kfCount; i++ ) {
		if ( kf[i].time <= targetTime ) {
			best = i;
		} else {
			break;
		}
	}
	return best;
}

/*
==================
CL_DemoCacheRestore

Restore the client state captured at keyframe i and give the cgame a clean
re-init at that point. The map stays loaded (CM_LoadMap / RE_LoadWorldMap no-op
for the same map), so there is no reload hitch; the cgame's snapshot/time
tracking resets, so no number/time faking is needed and the demo time is real.
==================
*/
static qboolean CL_DemoCacheRestore( int i ) {
	fileHandle_t df;

	if ( i < 0 || i >= kfCount ) {
		return qfalse;
	}

	df = clc.demofile;			// the live handle is constant; keep it
	cl = kf[i].cl;
	clc = kf[i].clc;
	clc.demofile = df;
	FS_Seek( clc.demofile, kf[i].offset, FS_SEEK_SET );

	// hand the cgame a clean slate at this demo position. The map loads inside
	// CG_INIT are no-ops (same map still resident), so this is cheap and has no
	// reload flash. cl_demoReinit tells CL_InitCGame to skip its first-load-only
	// paging work (Com_TouchMemory / EndRegistration).
	cl_demoReinit = qtrue;
	CL_ShutdownCGame();
	CL_InitCGame();				// leaves cls.state = CA_PRIMED
	cl_demoReinit = qfalse;

	// drive the demo state machine straight to active THIS frame (no loading
	// flash): firstDemoFrameSkipped = qtrue makes CL_SetCGameTime's PRIMED path
	// read the next message -> CL_FirstSnapshot -> CA_ACTIVE immediately, then
	// the seek fast-forwards to the target in the same frame. The first-frame
	// skip only exists to hide a real gamestate-load delay, which we don't have.
	clc.firstDemoFrameSkipped = qtrue;
	cl.newSnapshots = qfalse;

	S_StopAllSounds();
	return qtrue;
}

/*
==================
CL_DemoCacheSeekTo

Position playback so the next read-ahead reaches demo time `target`. We ALWAYS
restore the nearest cached keyframe at/under the target and re-init the cgame
there, then let the read-ahead fast-forward the small remainder (< one keyframe
interval). This applies to BOTH directions on purpose: a forward seek that just
fast-forwarded the engine would make the cgame walk cg.snap across a big gap
through the 32-deep snapshot ring - the intermediate snapshots have fallen out,
so it stalls on "Connection Interrupted". Re-initing from a nearby keyframe
gives the cgame a clean bracket at the target with no gap to walk. The re-init
is cheap (the map stays loaded) and leaves the last frame on screen, so it is
not visible. The caller then sets cl_demoSeek = target to drive the fast-forward.
==================
*/
void CL_DemoCacheSeekTo( int target ) {
	int idx = CL_DemoCacheFind( target );	// nearest keyframe at/under the target
	CL_DemoCacheRestore( idx < 0 ? 0 : idx );
}
