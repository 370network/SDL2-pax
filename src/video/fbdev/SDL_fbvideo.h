/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2014 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef __SDL_FBVIDEO_H__
#define __SDL_FBVIDEO_H__

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

typedef struct SDL_WindowData
{
    int fb_fd;           
    Uint8 *fb_mem;       
    size_t fb_mem_len;   
    int fb_pitch;        
    SDL_Surface *surface;
} SDL_WindowData;

/****************************************************************************/
/* SDL_VideoDevice functions declaration                                    */
/****************************************************************************/

/* Display and window functions */
int FB_VideoInit(_THIS);
void FB_VideoQuit(_THIS);
void FB_GetDisplayModes(_THIS, SDL_VideoDisplay * display);
int FB_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode);
int FB_CreateWindow(_THIS, SDL_Window * window);
int FB_CreateWindowFrom(_THIS, SDL_Window * window, const void *data);
void FB_SetWindowTitle(_THIS, SDL_Window * window);
void FB_SetWindowIcon(_THIS, SDL_Window * window, SDL_Surface * icon);
void FB_SetWindowPosition(_THIS, SDL_Window * window);
void FB_SetWindowSize(_THIS, SDL_Window * window);
void FB_ShowWindow(_THIS, SDL_Window * window);
void FB_HideWindow(_THIS, SDL_Window * window);
void FB_RaiseWindow(_THIS, SDL_Window * window);
void FB_MaximizeWindow(_THIS, SDL_Window * window);
void FB_MinimizeWindow(_THIS, SDL_Window * window);
void FB_RestoreWindow(_THIS, SDL_Window * window);
void FB_SetWindowGrab(_THIS, SDL_Window * window, SDL_bool grabbed);
void FB_DestroyWindow(_THIS, SDL_Window * window);

/* Window manager function */
SDL_bool FB_GetWindowWMInfo(_THIS, SDL_Window * window,
                             struct SDL_SysWMinfo *info);

/* fbdev create/update/destroywindowfb, based on dummy */
int FB_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format, void ** pixels, int *pitch);
int FB_UpdateWindowFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects);
void FB_DestroyWindowFramebuffer(_THIS, SDL_Window * window);

#endif /* __SDL_FBVIDEO_H__ */

/* vi: set ts=4 sw=4 expandtab: */
