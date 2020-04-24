/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef EVENTS_H_
#define EVENTS_H_

#include <ginput.h>
#include <gimxpoll/include/gpoll.h>

#ifdef WIN32
#define SDLINPUT_WINDOW_NAME "SDLInputMsgWindow"

#define RAWINPUT_CLASS_NAME "RawInputCatcher"
#define RAWINPUT_WINDOW_NAME "RawInputMsgWindow"
#endif

#define MAX_EVENTS 256

struct mkb_source {
    int (* init)(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*));
    int (* get_src)();
    int (* grab)(int mode);
    const char * (* get_mouse_name)(int id);
    const char * (* get_keyboard_name)(int id);
    int (* sync_process)();
    void (* quit)();
};

void ev_register_mkb_source(struct mkb_source * source);

struct js_source {
    int (* init)(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*));
    const char * (* get_name)(int joystick);
    int (* add)(const char * name, unsigned int effects, int (*haptic_cb)(const GE_Event * event));
    int (* get_haptic)(int joystick);
    int (* set_haptic)(const GE_Event * haptic);
    void * (* get_hid)(int joystick);
	int (* get_usb_ids)(int joystick, unsigned short * vendor, unsigned short * product);
    int (* close)(int joystick);
    int (* sync_process)();
    void (* quit)();
};

void ev_register_js_source(struct js_source * source);

int ev_init(const GPOLL_INTERFACE * poll_interface, unsigned char mkb_src, int(*callback)(GE_Event*));
void ev_quit();

int ev_joystick_register(const char* name, unsigned int effects, int (*haptic_cb)(const GE_Event * event));
void ev_joystick_close(int);
const char* ev_joystick_name(int);
const char* ev_mouse_name(int);
const char* ev_keyboard_name(int);

int ev_joystick_get_haptic(int joystick);
int ev_joystick_set_haptic(const GE_Event * event);

#ifndef WIN32
void * ev_joystick_get_hid(int joystick);
#else
int ev_joystick_get_usb_ids(int joystick, unsigned short * vendor, unsigned short * product);
#endif

int ev_grab_input(int);
void ev_pump_events();
void ev_sync_process();

#endif /* EVENTS_H_ */
