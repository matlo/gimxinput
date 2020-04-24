/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <ginput.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxcommon/include/gerror.h>
#include <gimxcommon/include/glist.h>
#include <gimxlog/include/glog.h>
#include "../events.h"

GLOG_GET(GLOG_NAME)

static Display* dpy = NULL;
static Window win;
static int xi_opcode;

#define DEVTYPE_KEYBOARD 0x01
#define DEVTYPE_MOUSE    0x02

static GPOLL_REMOVE_FD fp_remove = NULL;

static int (*event_callback)(GE_Event*) = NULL;

static struct
{
  int x;
  int y;
} mouse_coordinates;

struct xinput_device
{
  int mouse;
  int keyboard;
  char* name;
  unsigned int index;
  GLIST_LINK(struct xinput_device);
};

static struct xinput_device * device_index[GE_MAX_DEVICES];

static void xinput_quit();

static int xinput_close(void * user) {

    struct xinput_device * device = (struct xinput_device *) user;

    if (device->index < sizeof(device_index) / sizeof(*device_index)) {
        device_index[device->index] = NULL;
    }

    free(device->name);

    GLIST_REMOVE(x_devices, device);

    free(device);

    return 1;
}

static GLIST_INST(struct xinput_device, x_devices);

static inline uint8_t get_button(int detail) {

    switch (detail) {
    case 1:
        return GE_BTN_LEFT;
    case 2:
        return GE_BTN_MIDDLE;
    case 3:
        return GE_BTN_RIGHT;
    case 4:
        return GE_BTN_WHEELUP;
    case 5:
        return GE_BTN_WHEELDOWN;
    case 6:
        return GE_BTN_WHEELRIGHT;
    case 7:
        return GE_BTN_WHEELLEFT;
    case 8:
        return GE_BTN_BACK;
    case 9:
        return GE_BTN_FORWARD;
    default:
        return 0xff;
    }
}

static void xinput_process_event(XIRawEvent* revent) {

    GE_Event evt = { };
    int i;

    //ignore events from master device
    if (revent->deviceid != revent->sourceid || revent->sourceid >= (int) (sizeof(device_index) / sizeof(*device_index))) {
        return;
    }

    struct xinput_device * device = device_index[revent->sourceid];
    if (device == NULL) {
        return;
    }

    switch (revent->evtype) {
    case XI_RawMotion:
        evt.type = GE_MOUSEMOTION;
        evt.motion.which = device->mouse;
        i = 0;
        evt.motion.xrel = XIMaskIsSet(revent->valuators.mask, 0) ? revent->raw_values[i++] : 0;
        evt.motion.yrel = XIMaskIsSet(revent->valuators.mask, 1) ? revent->raw_values[i++] : 0;
        break;
    case XI_RawButtonPress:
        evt.type = GE_MOUSEBUTTONDOWN;
        evt.button.which = device->mouse;
        evt.button.button = get_button(revent->detail);
        break;
    case XI_RawButtonRelease:
        evt.type = GE_MOUSEBUTTONUP;
        evt.button.which = device->mouse;
        evt.button.button = get_button(revent->detail);
        break;
    case XI_RawKeyPress: {
        evt.type = GE_KEYDOWN;
        evt.button.which = device->keyboard;
        evt.button.button = revent->detail - 8;
        break;
    }
    case XI_RawKeyRelease: {
        evt.type = GE_KEYUP;
        evt.button.which = device->keyboard;
        evt.button.button = revent->detail - 8;
        break;
    }
    default:
        break;
    }

    /*
     * Process evt.
     */
    if (evt.type != GE_NOEVENT) {
        event_callback(&evt);
    }
}

static int xinput_process_events(void * user __attribute__((unused))) {

    XEvent ev;
    XGenericEventCookie *cookie = &ev.xcookie;

    while (XPending(dpy)) {
        XFlush(dpy);
        XPending(dpy);

        XNextEvent(dpy, &ev);
        if (XGetEventData(dpy, cookie)) {
            XIRawEvent* revent = cookie->data;

            if (cookie->type == GenericEvent && cookie->extension == xi_opcode) {
                xinput_process_event(revent);
            }

            XFreeEventData(dpy, cookie);
        }
    }

    return 0;
}

static Window create_win(Display *dpy) {

    XIEventMask mask;

    Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);

    mask.deviceid = XIAllDevices;
    mask.mask_len = XIMaskLen(XI_RawMotion);
    mask.mask = calloc(mask.mask_len, sizeof(char));

    XISetMask(mask.mask, XI_RawButtonPress);
    XISetMask(mask.mask, XI_RawButtonRelease);
    XISetMask(mask.mask, XI_RawKeyPress);
    XISetMask(mask.mask, XI_RawKeyRelease);
    XISetMask(mask.mask, XI_RawMotion);

    XISelectEvents(dpy, DefaultRootWindow(dpy), &mask, 1);

    free(mask.mask);
    XMapWindow(dpy, win);
    XSync(dpy, True);

    return win;
}

static int xinput_init(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*)) {

    int ret = 0;
    int event, error;
    int m_num = 0;
    int k_num = 0;

    if (callback == NULL) {
        PRINT_ERROR_OTHER("callback is NULL");
        return -1;
    }

    if (poll_interface->fp_register == NULL) {
        PRINT_ERROR_OTHER("fp_register is NULL");
        return -1;
    }

    if (poll_interface->fp_remove == NULL) {
        PRINT_ERROR_OTHER("fp_remove is NULL");
        return -1;
    }

    event_callback = callback;
    fp_remove = poll_interface->fp_remove;

    unsigned int i;
    for (i = 0; i < sizeof(device_index) / sizeof(*device_index); ++i) {
        device_index[i] = NULL;
    }

    dpy = XOpenDisplay(NULL);

    if (!dpy) {
        PRINT_ERROR_OTHER("Failed to open display.\n");
        return -1;
    }

    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) {
        PRINT_ERROR_OTHER("X Input extension not available.\n");
        return -1;
    }

    win = create_win(dpy);

    int nxdevices;
    XIDeviceInfo *xdevices, *xdevice;

    xdevices = XIQueryDevice(dpy, XIAllDevices, &nxdevices);

    for (i = 0; nxdevices > 0 && i < (unsigned int) nxdevices; i++) {

        xdevice = xdevices + i;

        if (xdevice->deviceid >= (int) (sizeof(device_index) / sizeof(*device_index))) {
            continue;
        }

        if (xdevice->use != XISlaveKeyboard && xdevice->use != XISlavePointer) {
            continue;
        }

        int hasKeys = 0, hasButtons = 0, hasAxes = 0;

        int j;
        for (j = 0; j < xdevice->num_classes; ++j) {
            switch (xdevice->classes[j]->type) {
            case XIKeyClass:
                hasKeys = 1;
                break;
            case XIButtonClass:
                hasButtons = 1;
                break;
            case XIValuatorClass:
                hasAxes = 1;
                break;
            default:
                break;
            }
        }

        if (!hasKeys && !hasButtons && !hasAxes) {
            continue;
        }

        struct xinput_device * device = calloc(1, sizeof(*device));
        if (device == NULL) {
            PRINT_ERROR_ALLOC_FAILED("calloc");
            continue;
        }

        device->mouse = -1;
        device->keyboard = -1;

        device_index[xdevice->deviceid] = device;
        device->index = xdevice->deviceid;
        device->name = strdup(xdevice->name);

        if (hasKeys) {
            device->keyboard = k_num;
            ++k_num;
        }
        if (hasButtons || hasAxes) {
            device->mouse = m_num;
            ++m_num;
        }

        GLIST_ADD(x_devices, device);
    }

    XIFreeDeviceInfo(xdevices);

    GPOLL_CALLBACKS callbacks = {
            .fp_read = xinput_process_events,
            .fp_write = NULL,
            .fp_close = xinput_close,
    };
    poll_interface->fp_register(ConnectionNumber(dpy), NULL, &callbacks);

    XEvent xevent;

    /* get info about current pointer position */
    XQueryPointer(dpy, DefaultRootWindow(dpy), &xevent.xbutton.root, &xevent.xbutton.window, &xevent.xbutton.x_root,
            &xevent.xbutton.y_root, &xevent.xbutton.x, &xevent.xbutton.y, &xevent.xbutton.state);

    mouse_coordinates.x = xevent.xbutton.x;
    mouse_coordinates.y = xevent.xbutton.y;

    return ret;
}

static void xinput_quit() {

    GLIST_CLEAN_ALL(x_devices, xinput_close)

    if (dpy) {
        fp_remove(ConnectionNumber(dpy));

        XDestroyWindow(dpy, win);

        XWarpPointer(dpy, None, DefaultRootWindow(dpy), 0, 0, 0, 0, mouse_coordinates.x, mouse_coordinates.y);

        XCloseDisplay(dpy);
        dpy = NULL;
    }
}

/*
 * Grab the pointer into the window.
 * This function retries until success or 500ms have elapsed.
 */
static int xinput_grab(int mode) {

    int i = 0;
    while (XGrabPointer(dpy, win, True, 0, GrabModeAsync, GrabModeAsync, win, None, CurrentTime) != GrabSuccess
            && i < 50) {
        usleep(10000);
        ++i;
    }

    return mode;
}

static char* get_name(unsigned char devtype, int index) {

    struct xinput_device * device = GLIST_BEGIN(x_devices);
    while (device != GLIST_END(x_devices)) {
        switch(devtype) {
        case DEVTYPE_MOUSE:
            if (device->mouse == index) {
                return device->name;
            }
            break;
        case DEVTYPE_KEYBOARD:
            if (device->keyboard == index) {
                return device->name;
            }
            break;
        }
        device = device->next;
    }
    return NULL;
}

const char* xinput_get_mouse_name(int index) {

    return get_name(DEVTYPE_MOUSE, index);
}

const char* xinput_get_keyboard_name(int index) {

    return get_name(DEVTYPE_KEYBOARD, index);
}

static int xinput_get_src() {

    return GE_MKB_SOURCE_WINDOW_SYSTEM;
}

static struct mkb_source source = {
    .init = xinput_init,
    .get_src = xinput_get_src,
    .grab = xinput_grab,
    .get_mouse_name = xinput_get_mouse_name,
    .get_keyboard_name = xinput_get_keyboard_name,
    .sync_process = NULL,
    .quit = xinput_quit,
};

void xinput_constructor() __attribute__((constructor));
void xinput_constructor() {
    ev_register_mkb_source(&source);
}

