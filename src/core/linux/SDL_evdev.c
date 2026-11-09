/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>

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

#ifdef SDL_INPUT_LINUXEV

/* This is based on the linux joystick driver */
/* References: https://www.kernel.org/doc/Documentation/input/input.txt 
 *             https://www.kernel.org/doc/Documentation/input/event-codes.txt
 *             /usr/include/linux/input.h
 *             The evtest application is also useful to debug the protocol
 */

#include "SDL_evdev.h"
#include "SDL_evdev_kbd.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#include "SDL.h"
#include "SDL_endian.h"
#include "SDL_scancode.h"
#include "../../events/SDL_events_c.h"
#include "../../events/scancodes_linux.h" /* adds linux_scancode_table */
#include "../../core/linux/SDL_evdev_capabilities.h"
#include "../../core/linux/SDL_udev.h"
#include "../../events/SDL_scancode_tables_c.h"

/* These are not defined in older Linux kernel headers */
#ifndef SYN_DROPPED
#define SYN_DROPPED 3
#endif
#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT         0x2f
#define ABS_MT_POSITION_X   0x35
#define ABS_MT_POSITION_Y   0x36
#define ABS_MT_TRACKING_ID  0x39
#define ABS_MT_PRESSURE     0x3a
#endif

typedef struct SDL_evdevlist_item
{
    char *path;
    int fd;

    /* TODO: use this for every device, not just touchscreen */
    int out_of_sync;

    SDL_bool touch_down;
    SDL_bool touch_last_down;
    SDL_bool touch_have_x;
    SDL_bool touch_have_y;
    SDL_bool touch_position_changed;
    int device_class;
    int mouse_x, mouse_y;

    /* TODO: expand on this to have data for every possible class (mouse,
       keyboard, touchpad, etc.). Also there's probably some things in here we
       can pull out to the SDL_evdevlist_item i.e. name */
    int is_touchscreen;
    struct {
        char* name;

        int min_x, max_x, range_x;
        int min_y, max_y, range_y;
        int min_pressure, max_pressure, range_pressure;

        int max_slots;
        int current_slot;
        struct {
            enum {
                EVDEV_TOUCH_SLOTDELTA_NONE = 0,
                EVDEV_TOUCH_SLOTDELTA_DOWN,
                EVDEV_TOUCH_SLOTDELTA_UP,
                EVDEV_TOUCH_SLOTDELTA_MOVE
            } delta;
            int tracking_id;
            int x, y, pressure;
        } * slots;

    } * touchscreen_data;

    struct SDL_evdevlist_item *next;
} SDL_evdevlist_item;

typedef struct SDL_EVDEV_PrivateData
{
    int ref_count;
    int num_devices;
    SDL_evdevlist_item *first;
    SDL_evdevlist_item *last;
    SDL_EVDEV_keyboard_state *kbd;
} SDL_EVDEV_PrivateData;

#undef _THIS
#define _THIS SDL_EVDEV_PrivateData *_this
static _THIS = NULL;

static SDL_Scancode SDL_EVDEV_translate_keycode(int keycode);
static void SDL_EVDEV_sync_device(SDL_evdevlist_item *item);
static int SDL_EVDEV_device_removed(const char *dev_path);

#if SDL_USE_LIBUDEV
static int SDL_EVDEV_device_added(const char *dev_path, int udev_class);
static void SDL_EVDEV_udev_callback(SDL_UDEV_deviceevent udev_type, int udev_class,
    const char *dev_path);
#endif /* SDL_USE_LIBUDEV */

static Uint8 EVDEV_MouseButtons[] = {
    SDL_BUTTON_LEFT,            /*  BTN_LEFT        0x110 */
    SDL_BUTTON_RIGHT,           /*  BTN_RIGHT       0x111 */
    SDL_BUTTON_MIDDLE,          /*  BTN_MIDDLE      0x112 */
    SDL_BUTTON_X1,              /*  BTN_SIDE        0x113 */
    SDL_BUTTON_X2,              /*  BTN_EXTRA       0x114 */
    SDL_BUTTON_X2 + 1,          /*  BTN_FORWARD     0x115 */
    SDL_BUTTON_X2 + 2,          /*  BTN_BACK        0x116 */
    SDL_BUTTON_X2 + 3           /*  BTN_TASK        0x117 */
};

static int
SDL_EVDEV_SetRelativeMouseMode(SDL_bool enabled)
{
    /* Mice already send relative events through this interface */
    return 0;
}


int
SDL_EVDEV_Init(void)
{
    if (_this == NULL) {
        _this = (SDL_EVDEV_PrivateData*)SDL_calloc(1, sizeof(*_this));
        if (_this == NULL) {
            return SDL_OutOfMemory();
        }

#if SDL_USE_LIBUDEV
        if (SDL_UDEV_Init() < 0) {
            SDL_free(_this);
            _this = NULL;
            return -1;
        }

        /* Set up the udev callback */
        if (SDL_UDEV_AddCallback(SDL_EVDEV_udev_callback) < 0) {
            SDL_UDEV_Quit();
            SDL_free(_this);
            _this = NULL;
            return -1;
        }

        /* Force a scan to build the initial device list */
        SDL_UDEV_Scan();
#else

        {
            /* Allow the user to specify a list of devices explicitly of
               the form:
                  deviceclass:path[,deviceclass:path[,...]]
               where device class is an integer representing the
               SDL_UDEV_deviceclass and path is the full path to
               the event device. */
            const char* devices = SDL_getenv("SDL_EVDEV_DEVICES");
            if (devices) {
                /* Assume this is the old use of the env var and it is not in
                   ROM. */
                char* rest = (char*) devices;
                char* spec;
                while ((spec = strtok_r(rest, ",", &rest))) {
                    char* endofcls = 0;
                    long cls = strtol(spec, &endofcls, 0);
                    if (endofcls)
                        SDL_EVDEV_device_added(endofcls + 1, cls);
                }
            }
            else {
                /* TODO: Scan the devices manually, like a caveman */
            }
        }
        
#endif /* SDL_USE_LIBUDEV */

        _this->kbd = SDL_EVDEV_kbd_init();
    }

    SDL_GetMouse()->SetRelativeMouseMode = SDL_EVDEV_SetRelativeMouseMode;

    _this->ref_count += 1;

    return 0;
}

void
SDL_EVDEV_Quit(void)
{
    if (_this == NULL) {
        return;
    }

    _this->ref_count -= 1;

    if (_this->ref_count < 1) {
#if SDL_USE_LIBUDEV
        SDL_UDEV_DelCallback(SDL_EVDEV_udev_callback);
        SDL_UDEV_Quit();
#endif /* SDL_USE_LIBUDEV */

        SDL_EVDEV_kbd_quit(_this->kbd);

        /* Remove existing devices */
        while(_this->first != NULL) {
            SDL_EVDEV_device_removed(_this->first->path);
        }

        SDL_assert(_this->first == NULL);
        SDL_assert(_this->last == NULL);
        SDL_assert(_this->num_devices == 0);

        SDL_free(_this);
        _this = NULL;
    }
}

#if SDL_USE_LIBUDEV
static void SDL_EVDEV_udev_callback(SDL_UDEV_deviceevent udev_event, int udev_class,
    const char* dev_path)
{
    if (dev_path == NULL) {
        return;
    }

    switch(udev_event) {
    case SDL_UDEV_DEVICEADDED:
        if (!(udev_class & (SDL_UDEV_DEVICE_MOUSE | SDL_UDEV_DEVICE_KEYBOARD |
            SDL_UDEV_DEVICE_TOUCHSCREEN)))
            return;

        SDL_EVDEV_device_added(dev_path, udev_class);
        break;  
    case SDL_UDEV_DEVICEREMOVED:
        SDL_EVDEV_device_removed(dev_path);
        break;
    default:
        break;
    }
}
#endif /* SDL_USE_LIBUDEV */

void 
SDL_EVDEV_Poll(void)
{
    struct input_event events[32];
    int i, j, len;
    SDL_evdevlist_item *item;
    SDL_Scancode scan_code;

    if (!_this) {
        return;
    }

#if SDL_USE_LIBUDEV
    SDL_UDEV_Poll();
#endif

    for (item = _this->first; item != NULL; item = item->next) {
        while ((len = read(item->fd, events, (sizeof events))) > 0) {
            len /= sizeof(events[0]);
            for (i = 0; i < len; ++i) {
            	struct input_event *event = &events[i];
            	
                if (item->out_of_sync) {
                    if (event->type == EV_SYN && event->code == SYN_REPORT) {
                        item->out_of_sync = SDL_FALSE;
                        SDL_EVDEV_sync_device(item);
                        /* Process this SYN_REPORT below. */
                    } else {
                        continue;
                    }
                }

                if (event->type == EV_SYN && event->code == SYN_DROPPED) {
                    if (item->is_touchscreen) {
                        item->out_of_sync = SDL_TRUE;
                    }
                    continue;
                }

                switch (event->type) {
                case EV_KEY:
                	if (item->is_touchscreen &&
                        (event->code == BTN_TOUCH || event->code == BTN_LEFT)) {
                        item->touch_down = (event->value != 0) ? SDL_TRUE : SDL_FALSE;
                        break;
                    }

                    if (item->device_class & 0x02) {
                        scan_code = SDL_EVDEV_translate_keycode(event->code);

                        if (scan_code != SDL_SCANCODE_UNKNOWN) {
                            if (event->value == 0) {
                                SDL_SendKeyboardKey(SDL_RELEASED, scan_code);
                            } else if (event->value == 1 || event->value == 2) {
                                SDL_SendKeyboardKey(SDL_PRESSED, scan_code);
                            }
                        }

                        if (_this->kbd) {
                            SDL_EVDEV_kbd_keycode(_this->kbd,event->code,event->value);
                        }
                    }
                    break;
                case EV_ABS:
                    if (!item->is_touchscreen) {
                        break;
                    }

                    switch (event->code) {
                    case ABS_X:
                    case ABS_MT_POSITION_X:
                        if (!item->touch_have_x || item->mouse_x != event->value) {
                            item->mouse_x = event->value;
                            item->touch_position_changed = SDL_TRUE;
                        }
                        item->touch_have_x = SDL_TRUE;
                        break;

                    case ABS_Y:
                    case ABS_MT_POSITION_Y:
                        if (!item->touch_have_y || item->mouse_y != event->value) {
                            item->mouse_y = event->value;
                            item->touch_position_changed = SDL_TRUE;
                        }
                        item->touch_have_y = SDL_TRUE;
                        break;

                    case ABS_MT_TRACKING_ID:
                        item->touch_down = (event->value >= 0) ? SDL_TRUE : SDL_FALSE;
                        break;

                    default:
                        break;
                    }
                    break;

                case EV_SYN:
                    if (event->code == SYN_REPORT && item->is_touchscreen) {
                        SDL_Window *win;

                        win = SDL_GetWindowFromID(1);
                        if (win) {
                            SDL_SetMouseFocus(win);
                            
                            if (item->touch_position_changed &&
                                item->touch_have_x && item->touch_have_y) {
                                SDL_SendMouseMotion(win, (SDL_MouseID)item->fd, 0, item->mouse_x, item->mouse_y);
                                item->touch_position_changed = SDL_FALSE;
                            }

                            if (item->touch_down != item->touch_last_down) {
                                SDL_SendMouseButton(win, (SDL_MouseID)item->fd, item->touch_down ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_LEFT);
                                item->touch_last_down = item->touch_down;
                            }
                        }
                    }
                    break;
                }
            }
        }    
    }
}

static SDL_Scancode
SDL_EVDEV_translate_keycode(int keycode)
{
    SDL_Scancode scancode = SDL_GetScancodeFromTable(SDL_SCANCODE_TABLE_LINUX, keycode);

#ifdef DEBUG_SCANCODES
    if (scancode == SDL_SCANCODE_UNKNOWN && keycode != BTN_TOUCH) {
        SDL_Log("The key you just pressed is not recognized by SDL. To help "
                "get this fixed, please report this to the SDL forums/mailing list "
                "<https://discourse.libsdl.org/> EVDEV KeyCode %d", keycode);
    }
#endif /* DEBUG_SCANCODES */

    return scancode;
}

#ifdef SDL_USE_LIBUDEV
static int
SDL_EVDEV_init_touchscreen(SDL_evdevlist_item* item)
{
    int ret, i;
    unsigned long xreq, yreq;
    char name[64];
    struct input_absinfo abs_info;

    if (!item->is_touchscreen)
        return 0;

    item->touchscreen_data = SDL_calloc(1, sizeof(*item->touchscreen_data));
    if (item->touchscreen_data == NULL)
        return SDL_OutOfMemory();

    ret = ioctl(item->fd, EVIOCGNAME(sizeof(name)), name);
    if (ret < 0) {
        SDL_free(item->touchscreen_data);
        return SDL_SetError("Failed to get evdev touchscreen name");
    }

    item->touchscreen_data->name = SDL_strdup(name);
    if (item->touchscreen_data->name == NULL) {
        SDL_free(item->touchscreen_data);
        return SDL_OutOfMemory();
    }

    ret = ioctl(item->fd, EVIOCGABS(ABS_MT_SLOT), &abs_info);
    if (ret < 0) {
        SDL_free(item->touchscreen_data->name);
        SDL_free(item->touchscreen_data);
        return SDL_SetError("Failed to get evdev touchscreen limits");
    }

    if (abs_info.maximum == 0) {
        item->touchscreen_data->max_slots = 1;
        xreq = EVIOCGABS(ABS_X);
        yreq = EVIOCGABS(ABS_Y);
    } else {
        item->touchscreen_data->max_slots = abs_info.maximum + 1;
        xreq = EVIOCGABS(ABS_MT_POSITION_X);
        yreq = EVIOCGABS(ABS_MT_POSITION_Y);
    }

    ret = ioctl(item->fd, xreq, &abs_info);
    if (ret < 0) {
        SDL_free(item->touchscreen_data->name);
        SDL_free(item->touchscreen_data);
        return SDL_SetError("Failed to get evdev touchscreen limits");
    }
    item->touchscreen_data->min_x = abs_info.minimum;
    item->touchscreen_data->max_x = abs_info.maximum;
    item->touchscreen_data->range_x = abs_info.maximum - abs_info.minimum;

    ret = ioctl(item->fd, yreq, &abs_info);
    if (ret < 0) {
        SDL_free(item->touchscreen_data->name);
        SDL_free(item->touchscreen_data);
        return SDL_SetError("Failed to get evdev touchscreen limits");
    }
    item->touchscreen_data->min_y = abs_info.minimum;
    item->touchscreen_data->max_y = abs_info.maximum;
    item->touchscreen_data->range_y = abs_info.maximum - abs_info.minimum;

    ret = ioctl(item->fd, EVIOCGABS(ABS_MT_PRESSURE), &abs_info);
    if (ret < 0) {
        SDL_free(item->touchscreen_data->name);
        SDL_free(item->touchscreen_data);
        return SDL_SetError("Failed to get evdev touchscreen limits");
    }
    item->touchscreen_data->min_pressure = abs_info.minimum;
    item->touchscreen_data->max_pressure = abs_info.maximum;
    item->touchscreen_data->range_pressure = abs_info.maximum - abs_info.minimum;

    item->touchscreen_data->slots = SDL_calloc(
        item->touchscreen_data->max_slots,
        sizeof(*item->touchscreen_data->slots));
    if (item->touchscreen_data->slots == NULL) {
        SDL_free(item->touchscreen_data->name);
        SDL_free(item->touchscreen_data);
        return SDL_OutOfMemory();
    }

    for(i = 0; i < item->touchscreen_data->max_slots; i++) {
        item->touchscreen_data->slots[i].tracking_id = -1;
    }

    ret = SDL_AddTouch(item->fd, /* I guess our fd is unique enough */
        SDL_TOUCH_DEVICE_DIRECT,
        item->touchscreen_data->name);
    if (ret < 0) {
        SDL_free(item->touchscreen_data->slots);
        SDL_free(item->touchscreen_data->name);
        SDL_free(item->touchscreen_data);
        return ret;
    }

    return 0;
}
#endif /* SDL_USE_LIBUDEV */

static void
SDL_EVDEV_destroy_touchscreen(SDL_evdevlist_item* item) {
    if (!item->is_touchscreen)
        return;

    SDL_DelTouch(item->fd);
    SDL_free(item->touchscreen_data->slots);
    SDL_free(item->touchscreen_data->name);
    SDL_free(item->touchscreen_data);
}

static void
SDL_EVDEV_sync_device(SDL_evdevlist_item *item)
{
    struct input_absinfo abs_info;
    unsigned long keybits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1];
    const unsigned int bits_per_long = (unsigned int)(sizeof(unsigned long) * 8);
    int ret;

    if (!item->is_touchscreen) {
        return;
    }

    ret = ioctl(item->fd, EVIOCGABS(ABS_X), &abs_info);
    if (ret < 0) {
        ret = ioctl(item->fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_info);
    }
    if (ret >= 0) {
        item->mouse_x = abs_info.value;
        item->touch_have_x = SDL_TRUE;
    }

    ret = ioctl(item->fd, EVIOCGABS(ABS_Y), &abs_info);
    if (ret < 0) {
        ret = ioctl(item->fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_info);
    }
    if (ret >= 0) {
        item->mouse_y = abs_info.value;
        item->touch_have_y = SDL_TRUE;
    }

    if (item->touch_have_x && item->touch_have_y) {
        item->touch_position_changed = SDL_TRUE;
    }

    SDL_memset(keybits, 0, sizeof(keybits));
    ret = ioctl(item->fd, EVIOCGKEY(sizeof(keybits)), keybits);
    if (ret >= 0) {
        SDL_bool btn_touch;
        SDL_bool btn_left;

        btn_touch = (keybits[BTN_TOUCH / bits_per_long] &
                     (1UL << (BTN_TOUCH % bits_per_long))) ? SDL_TRUE : SDL_FALSE;
        btn_left = (keybits[BTN_LEFT / bits_per_long] &
                    (1UL << (BTN_LEFT % bits_per_long))) ? SDL_TRUE : SDL_FALSE;
        item->touch_down = (btn_touch || btn_left) ? SDL_TRUE : SDL_FALSE;
    }
}

int
SDL_EVDEV_device_added(const char *dev_path, int device_class)
{
    SDL_evdevlist_item *item;

    /* Check to make sure this device isn't already in the list. */
    for (item = _this->first; item != NULL; item = item->next) {
        if (SDL_strcmp(dev_path, item->path) == 0) {
            return -1;
        }
    }

    item = (SDL_evdevlist_item *)SDL_calloc(1, sizeof(SDL_evdevlist_item));
    if (item == NULL) {
        return SDL_OutOfMemory();
    }

    item->fd = open(dev_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (item->fd < 0) {
        SDL_free(item);
        return SDL_SetError("Unable to open %s", dev_path);
    }

    item->path = SDL_strdup(dev_path);
    if (item->path == NULL) {
        close(item->fd);
        SDL_free(item);
        return SDL_OutOfMemory();
    }

    item->device_class = device_class;
    item->is_touchscreen = (device_class & 0x10) ? SDL_TRUE : SDL_FALSE;

    if (_this->last == NULL) {
        _this->first = _this->last = item;
    } else {
        _this->last->next = item;
        _this->last = item;
    }

    SDL_EVDEV_sync_device(item);

    return _this->num_devices++;
}

static int
SDL_EVDEV_device_removed(const char *dev_path)
{
    SDL_evdevlist_item *item;
    SDL_evdevlist_item *prev = NULL;

    for (item = _this->first; item != NULL; item = item->next) {
        /* found it, remove it. */
        if (SDL_strcmp(dev_path, item->path) == 0) {
            if (prev != NULL) {
                prev->next = item->next;
            } else {
                SDL_assert(_this->first == item);
                _this->first = item->next;
            }
            if (item == _this->last) {
                _this->last = prev;
            }
            if (item->is_touchscreen) {
                SDL_EVDEV_destroy_touchscreen(item);
            }
            close(item->fd);
            SDL_free(item->path);
            SDL_free(item);
            _this->num_devices--;
            return 0;
        }
        prev = item;
    }

    return -1;
}


#endif /* SDL_INPUT_LINUXEV */

/* vi: set ts=4 sw=4 expandtab: */
