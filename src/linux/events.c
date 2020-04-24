/*
 Copyright (c) 2010 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "../events.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <math.h>
#include <string.h>

#include <ginput.h>
#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>

GLOG_GET(GLOG_NAME)

static int mkb_source = -1;

struct mkb_source * source_physical = NULL;
struct mkb_source * source_window = NULL;

struct mkb_source * mkbsource = NULL;

void ev_register_mkb_source(struct mkb_source * source) {

    switch (source->get_src()) {
    case GE_MKB_SOURCE_PHYSICAL:
        source_physical = source;
        break;
    case GE_MKB_SOURCE_WINDOW_SYSTEM:
        source_window = source;
        break;
    }
}

#define CHECK_MKB_SOURCE(RETVAL) \
    do { \
        if (mkbsource == NULL) { \
            PRINT_ERROR_OTHER("no mkb source available"); \
            return RETVAL; \
        } \
    } while (0)

struct js_source * jsource = NULL;

void ev_register_js_source(struct js_source * source) {

    jsource = source;
}

#define CHECK_JS_SOURCE(RETVAL) \
    do { \
        if (jsource == NULL) { \
            PRINT_ERROR_OTHER("no joystick source available"); \
            return RETVAL; \
        } \
    } while (0)

int ev_init(const GPOLL_INTERFACE * poll_interface, unsigned char mkb_src, int (*callback)(GE_Event*)) {

    mkb_source = mkb_src;

    if (callback == NULL) {
        PRINT_ERROR_OTHER("callback is NULL");
        return -1;
    }

    if (mkb_source == GE_MKB_SOURCE_PHYSICAL) {
        mkbsource = source_physical;
        if (mkbsource == NULL) {
            PRINT_ERROR_OTHER("no physical mkb source available");
            return -1;
        }
    } else if (mkb_source == GE_MKB_SOURCE_WINDOW_SYSTEM) {
        mkbsource = source_window;
        if (mkbsource == NULL) {
            PRINT_ERROR_OTHER("no window mkb source available");
            return -1;
        }
    }

    if (mkbsource != NULL) {
        if (mkbsource->init(poll_interface, callback) < 0) {
            return -1;
        }
    }

    if (jsource == NULL) {
        PRINT_ERROR_OTHER("no joystick source available");
    } else {
        if (jsource->init(poll_interface, callback) < 0) {
            return -1;
        }
    }

    return 0;
}

int ev_joystick_register(const char* name, unsigned int effects, int (*haptic_cb)(const GE_Event * event)) {

    CHECK_JS_SOURCE(-1);

    return jsource->add(name, effects, haptic_cb);
}

void ev_joystick_close(int id) {

    CHECK_JS_SOURCE();

    jsource->close(id);
}

int ev_grab_input(int mode) {

    CHECK_MKB_SOURCE(-1);

    return mkbsource->grab(mode);
}

void ev_quit(void) {

    if (mkbsource != NULL) {
        mkbsource->quit();
    }

    if (jsource != NULL) {
        jsource->quit();
    }
}

const char* ev_joystick_name(int index) {

    CHECK_JS_SOURCE(NULL);

    return jsource->get_name(index);
}

const char* ev_mouse_name(int id) {

    CHECK_MKB_SOURCE(NULL);

    return mkbsource->get_mouse_name(id);
}

const char* ev_keyboard_name(int id) {

    CHECK_MKB_SOURCE(NULL);

    return mkbsource->get_keyboard_name(id);
}

int ev_joystick_get_haptic(int joystick) {

    CHECK_JS_SOURCE(-1);

    return jsource->get_haptic(joystick);
}

int ev_joystick_set_haptic(const GE_Event * event) {

    CHECK_JS_SOURCE(-1);

    return jsource->set_haptic(event);
}

void * ev_joystick_get_hid(int joystick) {

    CHECK_JS_SOURCE(NULL);

    if (jsource->get_hid == NULL) {
        return NULL;
    }

    return jsource->get_hid(joystick);
}

void ev_sync_process() {

    // All inputs are asynchronous on Linux!
}
