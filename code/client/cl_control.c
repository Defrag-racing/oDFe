/*
===========================================================================
oDFe launcher control channel.

A tiny localhost TCP server that lets the defrag-launcher drive the engine
while a demo plays embedded in the launcher window (variant B demo player).
The launcher connects to 127.0.0.1:<in_controlPort> and exchanges newline-
delimited text:

  launcher -> engine : any console command line (executed verbatim), e.g.
                       "timescale 0"  (pause)
                       "timescale 1"  (play)
                       "timescale 0.25" (slow-mo) / "timescale 4" (fast-fwd)
                       'demo "name"'   (load a demo)
                       later: a seek command (F3)
  engine -> launcher : periodic status line
                       "status time <ms> demo <0|1> paused <0|1>"
                     so the launcher's transport bar can show the playhead.

It is intentionally minimal and best-effort: bound to loopback only, single
client, non-blocking, and a no-op unless in_controlPort > 0. Winsock is
already initialised by NET_Init before CL_Init runs, so we don't touch it.
===========================================================================
*/

#include "client.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifndef socklen_t
typedef int control_socklen_t;
#    define socklen_t control_socklen_t
#  endif
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#  include <errno.h>
typedef int SOCKET;
#  define INVALID_SOCKET   (-1)
#  define SOCKET_ERROR     (-1)
#  define closesocket      close
#  define ioctlsocket      ioctl
#endif

static SOCKET	control_listen = INVALID_SOCKET;
static SOCKET	control_client = INVALID_SOCKET;
static int		control_port = 0;
static int		control_lastStatus = 0;
static qboolean	control_started = qfalse; // listener open attempted yet?

// Accumulates partial input until a full '\n'-terminated command arrives.
#define CONTROL_RXMAX 2048
static char		control_rx[CONTROL_RXMAX];
static int		control_rxlen = 0;

static void CL_Control_SetNonBlocking( SOCKET s ) {
	unsigned long on = 1;
	ioctlsocket( s, FIONBIO, &on );
}

static void CL_Control_CloseClient( void ) {
	if ( control_client != INVALID_SOCKET ) {
		closesocket( control_client );
		control_client = INVALID_SOCKET;
	}
	control_rxlen = 0;
}

/*
==================
CL_Control_Init

Just latch the configured port. The listener itself is opened lazily on the
first frame (CL_Control_OpenListener), because CL_Init() runs BEFORE NET_Init()
in Com_Init - so winsock (WSAStartup) isn't up yet here and socket() would
fail. No-op when in_controlPort <= 0 (the normal standalone case).
==================
*/
void CL_Control_Init( void ) {
	cvar_t			*cv;

	cv = Cvar_Get( "in_controlPort", "0", CVAR_LATCH );
	Cvar_SetDescription( cv, "Launcher demo-player control channel port (loopback TCP); 0 = disabled." );
	control_port = cv->integer;
}

/*
==================
CL_Control_OpenListener

Open the loopback listener. Called once from the first frame, by which time
NET_Init() has initialised winsock.
==================
*/
static void CL_Control_OpenListener( void ) {
	struct sockaddr_in addr;
	int				yes = 1;

	if ( control_port <= 0 || control_port > 65535 ) {
		return;
	}

	control_listen = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( control_listen == INVALID_SOCKET ) {
		Com_Printf( S_COLOR_YELLOW "control channel: socket() failed\n" );
		return;
	}

	setsockopt( control_listen, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof( yes ) );
	CL_Control_SetNonBlocking( control_listen );

	Com_Memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK ); // 127.0.0.1 only
	addr.sin_port = htons( (unsigned short)control_port );

	if ( bind( control_listen, (struct sockaddr *)&addr, sizeof( addr ) ) == SOCKET_ERROR ) {
		Com_Printf( S_COLOR_YELLOW "control channel: bind to port %d failed\n", control_port );
		closesocket( control_listen );
		control_listen = INVALID_SOCKET;
		return;
	}

	if ( listen( control_listen, 1 ) == SOCKET_ERROR ) {
		Com_Printf( S_COLOR_YELLOW "control channel: listen failed\n" );
		closesocket( control_listen );
		control_listen = INVALID_SOCKET;
		return;
	}

	Com_Printf( "control channel listening on 127.0.0.1:%d\n", control_port );
}

/*
==================
CL_Control_Shutdown
==================
*/
void CL_Control_Shutdown( void ) {
	CL_Control_CloseClient();
	if ( control_listen != INVALID_SOCKET ) {
		closesocket( control_listen );
		control_listen = INVALID_SOCKET;
	}
	control_port = 0;
	control_started = qfalse;
}

// Run one complete command line from the launcher. Trailing CR is trimmed.
// Everything is handed to the console; the launcher speaks console-command.
static void CL_Control_RunLine( char *line ) {
	size_t len = strlen( line );
	while ( len > 0 && ( line[len - 1] == '\r' || line[len - 1] == ' ' ) ) {
		line[--len] = '\0';
	}
	if ( len == 0 ) {
		return;
	}
	Cbuf_ExecuteText( EXEC_APPEND, va( "%s\n", line ) );
}

// Pull all complete lines out of the receive accumulator.
static void CL_Control_DrainLines( void ) {
	int start = 0;
	int i;
	for ( i = 0; i < control_rxlen; i++ ) {
		if ( control_rx[i] == '\n' ) {
			control_rx[i] = '\0';
			CL_Control_RunLine( &control_rx[start] );
			start = i + 1;
		}
	}
	// keep the unfinished tail
	if ( start > 0 ) {
		control_rxlen -= start;
		if ( control_rxlen > 0 ) {
			memmove( control_rx, &control_rx[start], control_rxlen );
		}
	}
	// runaway line with no newline: drop it so we don't deadlock the buffer
	if ( control_rxlen >= CONTROL_RXMAX - 1 ) {
		control_rxlen = 0;
	}
}

/*
==================
CL_Control_SendKey

Forward a transport key the engine window swallowed while it held keyboard
focus (embedded demo playback) up to the launcher, so its shortcut handler
runs even though the WebView never saw the keydown. Names are the normalized
tokens the launcher understands: "esc", "left", "right", "up", "down", "space".
Best-effort; silently ignored if no launcher is connected.
==================
*/
void CL_Control_SendKey( const char *name ) {
	char	msg[64];
	int		len;

	if ( control_client == INVALID_SOCKET ) {
		return;
	}
	len = Com_sprintf( msg, sizeof( msg ), "key %s\n", name );
	if ( send( control_client, msg, len, 0 ) == SOCKET_ERROR ) {
		// peer gone; the frame loop's recv will notice the close
	}
}

static void CL_Control_SendStatus( void ) {
	char	msg[128];
	int		len;
	int		paused;

	if ( control_client == INVALID_SOCKET ) {
		return;
	}

	// paused via the dedicated demo-pause flag or a 0 timescale (see cl_cgame.c).
	paused = ( cl_demoPaused || Cvar_VariableValue( "timescale" ) == 0.0f ) ? 1 : 0;

	// time/start/total are absolute server-times; the launcher's playhead
	// position is (time - start) and the demo length is (total - start).
	// total is 0 until measured (launcher seeks to the end once).
	len = Com_sprintf( msg, sizeof( msg ), "status time %d start %d total %d demo %d paused %d atend %d\n",
		cl.serverTime, cl_demoStartTime, cl_demoTotalTime,
		clc.demoplaying ? 1 : 0, paused, cl_demoAtEnd ? 1 : 0 );

	// best-effort; ignore would-block / errors
	if ( send( control_client, msg, len, 0 ) == SOCKET_ERROR ) {
		// if the peer is gone, recv below will catch the close too
	}
}

/*
==================
CL_Control_Frame

Polled once per client frame: accept a pending connection, ingest commands,
and push a status line a few times a second. Cheap when idle.
==================
*/
void CL_Control_Frame( void ) {
	int		n;
	int		now;

	// Lazily open the listener on the first frame (winsock is up by now).
	if ( !control_started ) {
		control_started = qtrue;
		CL_Control_OpenListener();
	}

	if ( control_listen == INVALID_SOCKET ) {
		return;
	}

	// accept a single launcher connection
	if ( control_client == INVALID_SOCKET ) {
		SOCKET s = accept( control_listen, NULL, NULL );
		if ( s != INVALID_SOCKET ) {
			control_client = s;
			CL_Control_SetNonBlocking( control_client );
			control_rxlen = 0;
		} else {
			return; // nobody connected yet
		}
	}

	// read everything currently available
	for ( ;; ) {
		char tmp[512];
		n = recv( control_client, tmp, sizeof( tmp ), 0 );
		if ( n > 0 ) {
			int space = CONTROL_RXMAX - 1 - control_rxlen;
			int copy = ( n < space ) ? n : space;
			if ( copy > 0 ) {
				memcpy( &control_rx[control_rxlen], tmp, copy );
				control_rxlen += copy;
			}
			CL_Control_DrainLines();
			continue;
		}
		if ( n == 0 ) {
			// peer closed
			CL_Control_CloseClient();
			return;
		}
		// n < 0: would-block (done for now) or a real error
		break;
	}

	// push status ~10x/sec so the launcher's playhead stays current
	now = Sys_Milliseconds();
	if ( now - control_lastStatus >= 100 || now < control_lastStatus ) {
		control_lastStatus = now;
		CL_Control_SendStatus();
	}
}
