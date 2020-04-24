/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "../events.h"

#include <ginput.h>
#include "../sdl/sdlinput.h"
#include <gimxlog/include/glog.h>

GLOG_GET(GLOG_NAME)

int ev_init(unsigned char mkb_src, int(*callback)(GE_Event*))
{
  if (callback == NULL) {
    PRINT_ERROR_OTHER("callback cannot be NULL");
    return -1;
  }

  if (mkb_src == GE_MKB_SOURCE_PHYSICAL)
  {
    PRINT_ERROR_OTHER("Physical events are not available on this platform.");
    return -1;
  }

  if(sdlinput_init(mkb_src, callback) < 0)
  {
    return -1;
  }

  return 0;
}

void ev_quit(void)
{
  sdlinput_quit();
}

const char* ev_joystick_name(int id)
{
  return sdlinput_joystick_name(id);
}

int ev_joystick_register(const char* name, unsigned int effects, int (*haptic_cb)(const GE_Event * haptic))
{
  return sdlinput_joystick_register(name, effects, haptic_cb);
}

/*
 * Close a joystick, and close the joystick subsystem if none is used anymore.
 */
void ev_joystick_close(int id)
{
  return sdlinput_joystick_close(id);
}

const char* ev_mouse_name(int id)
{
  if(id == 0)
  {
    return "Window Events";
  }
  return NULL;
}

const char* ev_keyboard_name(int id)
{
  if(id == 0)
  {
    return "Window Events";
  }
  return NULL;
}

int ev_grab_input(int mode)
{
  sdlinput_grab(mode);

  // TODO capture mouse if mode is GE_GRAB_ON, release mouse otherwise

  return 0;
}

void ev_sync_process()
{
  return sdlinput_sync_process();
}

int ev_joystick_get_haptic(int joystick)
{
  return sdlinput_joystick_get_haptic(joystick);
}

int ev_joystick_set_haptic(const GE_Event * event)
{
  return sdlinput_joystick_set_haptic(event);
}

int ev_joystick_get_usb_ids(int joystick, unsigned short * vendor, unsigned short * product)
{
  return sdlinput_joystick_get_usb_ids(joystick, vendor, product);
}
