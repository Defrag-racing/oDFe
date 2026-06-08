/*
===========================================================================
oDFe embedded-player support.

Lets the engine render into a window supplied by the Defrag Racing Launcher
(the launcher creates a "stage" window and passes its native id via
"+set in_embedParent <id>"). On Windows this is handled in win_glimp.c; this
header covers the SDL path used on Linux (X11 / XWayland), where we reparent
the SDL window into the launcher's X11 window.

These helpers deliberately live in their own translation unit (sdl_embed.c)
that pulls in <X11/Xlib.h>. Xlib #defines a pile of short names (None, Window,
Status, Bool, ...) that collide with engine/renderer identifiers, so we keep
that include out of sdl_glimp.c and expose only this tiny, type-clean API.
===========================================================================
*/
#ifndef SDL_EMBED_H
#define SDL_EMBED_H

struct SDL_Window;

/* 1 if this build can embed into an X11 parent (compiled with X11 SDL), else 0. */
int SDLEmbed_Available( void );

/* Query the launcher parent window's pixel size. Returns 1 and fills the w and h
   out-params on success, 0 otherwise (bad id, no X11, query failed). */
int SDLEmbed_ParentSize( unsigned long parent, int *w, int *h );

/* Reparent `win`'s native X11 window into `parent`, position it at the parent's
   origin, raise and map it. No-op (returns 0) if not X11 or anything fails;
   returns 1 on success. */
int SDLEmbed_Reparent( struct SDL_Window *win, unsigned long parent );

#endif /* SDL_EMBED_H */
