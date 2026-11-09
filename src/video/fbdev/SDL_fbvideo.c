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

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_FBDEV

#include "../SDL_sysvideo.h"
#include "SDL_version.h"
#include "SDL_syswm.h"
#include "SDL_loadso.h"
#include "SDL_events.h"

#ifdef SDL_INPUT_LINUXEV
#include "../../core/linux/SDL_evdev.h"
#endif

#include "SDL_fbvideo.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_keyboard_c.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static void
FB_PumpEvents(_THIS)
{
#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Poll();
#endif
}

static void
FB_Destroy(SDL_VideoDevice * device)
{
    if (device->driverdata != NULL) {
        device->driverdata = NULL;
    }
}

static SDL_VideoDevice *
FB_Create()
{
    SDL_VideoDevice *device;

    /* Initialize SDL_VideoDevice structure */
    device = (SDL_VideoDevice *) SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (device == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Setup amount of available displays and current display */
    device->num_displays = 0;

    /* Set device free function */
    device->free = FB_Destroy;

    /* Setup all functions which we can handle */
    device->VideoInit = FB_VideoInit;
    device->VideoQuit = FB_VideoQuit;
    device->GetDisplayModes = FB_GetDisplayModes;
    device->SetDisplayMode = FB_SetDisplayMode;
    device->CreateSDLWindow = FB_CreateWindow;
    device->CreateSDLWindowFrom = FB_CreateWindowFrom;
    device->SetWindowTitle = FB_SetWindowTitle;
    device->SetWindowIcon = FB_SetWindowIcon;
    device->SetWindowPosition = FB_SetWindowPosition;
    device->SetWindowSize = FB_SetWindowSize;
    device->ShowWindow = FB_ShowWindow;
    device->HideWindow = FB_HideWindow;
    device->RaiseWindow = FB_RaiseWindow;
    device->MaximizeWindow = FB_MaximizeWindow;
    device->MinimizeWindow = FB_MinimizeWindow;
    device->RestoreWindow = FB_RestoreWindow;
    device->SetWindowGrab = FB_SetWindowGrab;
    device->DestroyWindow = FB_DestroyWindow;
    device->GetWindowWMInfo = FB_GetWindowWMInfo;
    device->CreateWindowFramebuffer = FB_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = FB_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = FB_DestroyWindowFramebuffer;

    device->PumpEvents = FB_PumpEvents;

    return device;
}

VideoBootStrap FBDEV_bootstrap = {
    "fbdev",
    "Linux Framebuffer Video Driver",
    FB_Create
};

/*****************************************************************************/
/* SDL Video and Display initialization/handling functions                   */
/*****************************************************************************/
int
FB_VideoInit(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;

    SDL_zero(current_mode);

    /* XXX: Hardcoded for now */
    current_mode.w = 320;
    current_mode.h = 240;
    current_mode.refresh_rate = 60;
    current_mode.format = SDL_PIXELFORMAT_RGB565;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;

    if (SDL_AddVideoDisplay(&display, SDL_FALSE) < 0) {
        return -1;
    }

#ifdef SDL_INPUT_LINUXEV
    if (SDL_EVDEV_Init() < 0) {
        return -1;
    }
#endif

    return 0;
}

void
FB_VideoQuit(_THIS)
{
#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Quit();
#endif
}

void
FB_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
{
    /* Only one display mode available, the current one */
    SDL_AddDisplayMode(display, &display->current_mode);
}

int
FB_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
{
    return 0;
}

int
FB_CreateWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *wdata;
    SDL_VideoDisplay *display;

    /* Allocate window internal data */
    wdata = (SDL_WindowData *) SDL_calloc(1, sizeof(SDL_WindowData));
    if (wdata == NULL) {
        return SDL_OutOfMemory();
    }
    display = SDL_GetDisplayForWindow(window);

    /* Windows have one size for now */
    window->w = display->desktop_mode.w;
    window->h = display->desktop_mode.h;

    wdata->fb_fd = -1;

    /* Setup driver data for this window */
    window->driverdata = wdata;

    /* One window, it always has focus */
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);

    /* Window has been successfully created */
    return 0;
}

void
FB_DestroyWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *data;

    if(window->driverdata) {
        data = (SDL_WindowData *) window->driverdata;
        SDL_free(window->driverdata);
        window->driverdata = NULL;
    }
}

int
FB_CreateWindowFrom(_THIS, SDL_Window * window, const void *data)
{
    return -1;
}

void
FB_SetWindowTitle(_THIS, SDL_Window * window)
{
}
void
FB_SetWindowIcon(_THIS, SDL_Window * window, SDL_Surface * icon)
{
}
void
FB_SetWindowPosition(_THIS, SDL_Window * window)
{
}
void
FB_SetWindowSize(_THIS, SDL_Window * window)
{
}
void
FB_ShowWindow(_THIS, SDL_Window * window)
{
}
void
FB_HideWindow(_THIS, SDL_Window * window)
{
}
void
FB_RaiseWindow(_THIS, SDL_Window * window)
{
}
void
FB_MaximizeWindow(_THIS, SDL_Window * window)
{
}
void
FB_MinimizeWindow(_THIS, SDL_Window * window)
{
}
void
FB_RestoreWindow(_THIS, SDL_Window * window)
{
}
void
FB_SetWindowGrab(_THIS, SDL_Window * window, SDL_bool grabbed)
{

}

/*****************************************************************************/
/* SDL Window Manager function                                               */
/*****************************************************************************/
SDL_bool
FB_GetWindowWMInfo(_THIS, SDL_Window * window, struct SDL_SysWMinfo *info)
{
    if (info->version.major <= SDL_MAJOR_VERSION) {
        return SDL_TRUE;
    } else {
        SDL_SetError("application not compiled with SDL %d.%d\n",
                     SDL_MAJOR_VERSION, SDL_MINOR_VERSION);
        return SDL_FALSE;
    }

    /* Failed to get window manager information */
    return SDL_FALSE;
}

int
FB_CreateWindowFramebuffer(_THIS, SDL_Window * window, Uint32 * format, void ** pixels, int *pitch)
{
    SDL_WindowData *wdata = (SDL_WindowData *) window->driverdata;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    Uint32 px_format;

    if (wdata->fb_fd < 0) {
        wdata->fb_fd = open("/dev/fb", O_RDWR);
        if (wdata->fb_fd < 0) {
            return SDL_SetError("Could not open fb: %s", strerror(errno));
        }
    }

    if (ioctl(wdata->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(wdata->fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        return SDL_SetError("FBIOGET_*SCREENINFO on fb failed: %s", strerror(errno));
    }

    switch (vinfo.bits_per_pixel) {
        case 32:
            px_format = SDL_PIXELFORMAT_ARGB8888;
            break;
        case 24:
            px_format = SDL_PIXELFORMAT_RGB888;
            break;
        case 16:
        default:
            px_format = SDL_PIXELFORMAT_RGB565;
            break;
    }

    wdata->fb_mem_len = finfo.smem_len;
    wdata->fb_mem = (Uint8 *) mmap(NULL, wdata->fb_mem_len, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, wdata->fb_fd, 0);
    if (wdata->fb_mem == MAP_FAILED) {
        wdata->fb_mem = NULL;
        return SDL_SetError("mmap(fb) failed: %s", strerror(errno));
    }
    wdata->fb_pitch = finfo.line_length;

    wdata->surface = SDL_CreateRGBSurfaceWithFormat(0, window->w, window->h, 0, px_format);
    if (!wdata->surface) {
        return -1;
    }

    *format = px_format;
    *pixels = wdata->surface->pixels;
    *pitch = wdata->surface->pitch;

    return 0;
}

int
FB_UpdateWindowFramebuffer(_THIS, SDL_Window * window, const SDL_Rect * rects, int numrects)
{
    SDL_WindowData *wdata = (SDL_WindowData *) window->driverdata;
    int i;

    if (!wdata->fb_mem || !wdata->surface) {
        return SDL_SetError("Framebuffer not initialized");
    }

    for (i = 0; i < numrects; i++) {
        const SDL_Rect *rect = &rects[i];
        int bpp = wdata->surface->format->BytesPerPixel;
        int row_bytes = rect->w * bpp;
        int y;

        if (rect->w <= 0 || rect->h <= 0) {
            continue;
        }

        for (y = rect->y; y < rect->y + rect->h; y++) {
            Uint8 *src;
            Uint8 *dst;

            if (y < 0 || y >= window->h) {
                continue;
            }
            src = (Uint8 *) wdata->surface->pixels + y * wdata->surface->pitch + rect->x * bpp;
            dst = wdata->fb_mem + y * wdata->fb_pitch + rect->x * bpp;
            SDL_memcpy(dst, src, row_bytes);
        }
    }

    return 0;
}

void
FB_DestroyWindowFramebuffer(_THIS, SDL_Window * window)
{
    SDL_WindowData *wdata = (SDL_WindowData *) window->driverdata;

    if (!wdata) {
        return;
    }

    if (wdata->surface) {
        SDL_FreeSurface(wdata->surface);
        wdata->surface = NULL;
    }
    if (wdata->fb_mem) {
        munmap(wdata->fb_mem, wdata->fb_mem_len);
        wdata->fb_mem = NULL;
    }
    if (wdata->fb_fd >= 0) {
        close(wdata->fb_fd);
        wdata->fb_fd = -1;
    }
}

#endif /* SDL_VIDEO_DRIVER_FBDEV */

/* vi: set ts=4 sw=4 expandtab: */
