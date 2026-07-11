/*
===========================================================================
oDFe embedded-player support (SDL / X11).

See sdl_embed.h. This file is intentionally isolated from the rest of the
engine: it includes only SDL + Xlib, never the Quake headers, because Xlib
#defines names (None, Window, Status, Bool, ...) that clash with engine code.

The launcher (Defrag Racing Launcher) creates a small "stage" window inside
its own window and passes that window's X11 id via "+set in_embedParent <id>".
We take the SDL window the engine just created, reparent its X11 window into
the stage, and map it filling the stage. X11 allows reparenting into a window
owned by another client (the launcher) as long as we have its id, so this
works cross-process. Under Wayland the same path works through XWayland,
provided the launcher also runs on the X11 backend (GDK_BACKEND=x11).
===========================================================================
*/

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#	include "SDL_syswm.h"
#else
#	include <SDL.h>
#	include <SDL_syswm.h>
#endif

#include "sdl_embed.h"

#include <stdio.h>

#if defined(SDL_VIDEO_DRIVER_X11)
#include <X11/Xlib.h>

int SDLEmbed_Available( void )
{
	return 1;
}

int SDLEmbed_ParentSize( unsigned long parent, int *w, int *h )
{
	Display *dpy;
	Window root;
	int x, y;
	unsigned int pw, ph, bw, depth;

	if ( parent == 0 || w == NULL || h == NULL )
		return 0;

	/* Open our own short-lived connection to the same display the launcher
	   uses (DISPLAY env). We only need a couple of synchronous queries. */
	dpy = XOpenDisplay( NULL );
	if ( dpy == NULL )
		return 0;

	if ( !XGetGeometry( dpy, (Window)parent, &root, &x, &y, &pw, &ph, &bw, &depth ) )
	{
		XCloseDisplay( dpy );
		return 0;
	}

	XCloseDisplay( dpy );

	if ( pw == 0 || ph == 0 )
		return 0;

	*w = (int)pw;
	*h = (int)ph;
	return 1;
}

int SDLEmbed_Reparent( struct SDL_Window *win, unsigned long parent )
{
	SDL_SysWMinfo info;
	Display *dpy;
	Window self;

	if ( win == NULL || parent == 0 )
		return 0;

	/* Field reports (Zorin 18.1/GNOME) showed this failing while the
	   ParentSize query above worked, so log exactly which step breaks -
	   the launcher captures our stderr/stdout in its terminal output. */
	SDL_VERSION( &info.version );
	if ( !SDL_GetWindowWMInfo( (SDL_Window *)win, &info ) )
	{
		fprintf( stderr, "[embed] SDL_GetWindowWMInfo failed (compiled SDL %d.%d.%d): %s\n",
			info.version.major, info.version.minor, info.version.patch, SDL_GetError() );
		return 0;
	}

	if ( info.subsystem != SDL_SYSWM_X11 )
	{
		fprintf( stderr, "[embed] window subsystem is %d, not X11 (%d)\n",
			(int)info.subsystem, (int)SDL_SYSWM_X11 );
		return 0;
	}

	dpy = info.info.x11.display;
	self = info.info.x11.window;
	if ( dpy == NULL || self == 0 )
	{
		fprintf( stderr, "[embed] WMInfo returned dpy=%p window=0x%lx\n",
			(void *)dpy, (unsigned long)self );
		return 0;
	}

	/* Move our window under the launcher's stage. Reparenting away from the
	   root drops any window-manager decoration automatically. */
	XReparentWindow( dpy, self, (Window)parent, 0, 0 );
	XRaiseWindow( dpy, self );
	XMapWindow( dpy, self );
	XSync( dpy, False );
	return 1;
}

#else /* no X11 SDL backend (e.g. macOS, Windows-SDL) - embed unsupported */

int SDLEmbed_Available( void )
{
	return 0;
}

int SDLEmbed_ParentSize( unsigned long parent, int *w, int *h )
{
	(void)parent; (void)w; (void)h;
	return 0;
}

int SDLEmbed_Reparent( struct SDL_Window *win, unsigned long parent )
{
	(void)win; (void)parent;
	return 0;
}

#endif
