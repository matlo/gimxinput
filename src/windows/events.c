/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "../events.h"

#include <windows.h>

#include <ginput.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>

GLOG_GET(GLOG_NAME)

static int mkb_source = -1;

struct mkb_source * source_physical = NULL;
struct mkb_source * source_window = NULL;
struct mkb_source * source_llhooks = NULL;

struct mkb_source * mkbsource = NULL;

void ev_register_mkb_source(struct mkb_source * source)
{
    switch (source->get_src())
    {
    case GE_MKB_SOURCE_PHYSICAL:
        source_physical = source;
        break;
    case GE_MKB_SOURCE_WINDOW_SYSTEM:
        source_window = source;
        break;
    case GE_MKB_SOURCE_LOW_LEVEL_HOOKS:
        source_llhooks = source;
        break;
    }
}

#define CHECK_MKB_SOURCE(RETVAL) \
    if (mkbsource == NULL) { \
        PRINT_ERROR_OTHER("no mkb source available") \
        return RETVAL; \
    }

struct js_source * jsource = NULL;

void ev_register_js_source(struct js_source * source)
{
    jsource = source;
}

#define CHECK_JS_SOURCE(RETVAL) \
    if (jsource == NULL) { \
        PRINT_ERROR_OTHER("no joystick source available") \
        return RETVAL; \
    }

int ev_init(const GPOLL_INTERFACE * poll_interface __attribute__((unused)), unsigned char mkb_src, int(*callback)(GE_Event*))
{
  mkb_source = mkb_src;

  if (callback == NULL)
  {
    PRINT_ERROR_OTHER("callback is NULL")
    return -1;
  }

  if (mkb_source == GE_MKB_SOURCE_PHYSICAL)
  {
    mkbsource = source_physical;
    if (mkbsource == NULL)
    {
      PRINT_ERROR_OTHER("no physical mkb source available")
      return -1;
    }
    gpoll_set_rawinput_callback(mkbsource->sync_process);
  }
  else if (mkb_source == GE_MKB_SOURCE_WINDOW_SYSTEM)
  {
    mkbsource = source_window;
    if (mkbsource == NULL)
    {
      PRINT_ERROR_OTHER("no window mkb source available")
      return -1;
    }
  }
  else if (mkb_source == GE_MKB_SOURCE_LOW_LEVEL_HOOKS)
  {
    mkbsource = source_llhooks;
    if (mkbsource == NULL)
    {
      PRINT_ERROR_OTHER("no low level hooks mkb source available")
      return -1;
    }
  }

  if (mkbsource != NULL)
  {
    if (mkbsource->init(poll_interface, callback) < 0)
    {
      return -1;
    }
  }

  if (jsource == NULL)
  {
    PRINT_ERROR_OTHER("no joystick source available")
  }
  else
  {
    if (jsource->init(poll_interface, callback) < 0)
    {
      return -1;
    }
  }

  return 0;
}

void ev_quit(void)
{
  ev_grab_input(GE_GRAB_OFF);

  if (mkbsource != NULL)
  {
    mkbsource->quit();
  }

  if (jsource != NULL)
  {
    jsource->quit();
  }
}

const char* ev_joystick_name(int id)
{
  CHECK_JS_SOURCE(NULL)

  return jsource->get_name(id);
}

int ev_joystick_register(const char* name, unsigned int effects, int (*haptic_cb)(const GE_Event * event))
{
  CHECK_JS_SOURCE(-1)

  return jsource->add(name, effects, haptic_cb);
}

/*
 * Close a joystick, and close the joystick subsystem if none is used anymore.
 */
void ev_joystick_close(int id)
{
  CHECK_JS_SOURCE()

  jsource->close(id);
}

const char* ev_mouse_name(int id)
{
  CHECK_MKB_SOURCE(NULL)

  return mkbsource->get_mouse_name(id);
}

const char* ev_keyboard_name(int id)
{
  CHECK_MKB_SOURCE(NULL)

  return mkbsource->get_keyboard_name(id);
}

void ev_grab_input(int mode)
{
  if (mkb_source == GE_MKB_SOURCE_LOW_LEVEL_HOOKS)
  {
    return;
  }

  if(mode == GE_GRAB_ON)
  {
    HWND hwnd = NULL;

    if(mkb_source == GE_MKB_SOURCE_PHYSICAL)
    {
      hwnd = FindWindow(RAWINPUT_CLASS_NAME, RAWINPUT_WINDOW_NAME);
    }
    else if(mkb_source == GE_MKB_SOURCE_WINDOW_SYSTEM)
    {
      SDL_SetRelativeMouseMode(SDL_TRUE);

      hwnd = FindWindow(NULL, SDLINPUT_WINDOW_NAME);
    }

    if(hwnd)
    {
      // Reading from stdin before initializing ginput prevents the capture window from reaching the foreground...
      // This is a hack to work-around this issue.
      ShowWindow(hwnd, SW_MINIMIZE);
      ShowWindow(hwnd, SW_RESTORE);

      //clip the mouse cursor into the window
      RECT _clip;

      if(GetWindowRect(hwnd, &_clip))
      {
        ClipCursor(&_clip);
      }

      int i = 10;
      while(i > 0 && ShowCursor(FALSE) >= 0) { i--; }
    }
  }
  else
  {
    ClipCursor(NULL);

    int i = 10;
    while(i > 0 && ShowCursor(TRUE) < 0) { i--; }

    if(mkb_source == GE_MKB_SOURCE_WINDOW_SYSTEM)
    {
      SDL_SetRelativeMouseMode(SDL_FALSE);
    }
  }
}

void ev_sync_process()
{
  if (jsource != NULL && jsource->sync_process != NULL)
  {
    jsource->sync_process();
  }
  if (mkbsource != source_physical // do not call mkbsource->sync_process if it is the rawinput callback
          && mkbsource != NULL && mkbsource->sync_process != NULL)
  {
    mkbsource->sync_process();
  }
}

int ev_joystick_get_haptic(int joystick)
{
  CHECK_JS_SOURCE(-1)

  if (jsource->get_haptic != NULL)
  {
    return jsource->get_haptic(joystick);
  }

  return -1;
}

int ev_joystick_set_haptic(const GE_Event * event)
{
  CHECK_JS_SOURCE(-1)

  if (jsource->set_haptic != NULL)
  {
    return jsource->set_haptic(event);
  }
  
  return -1;
}

int ev_joystick_get_usb_ids(int joystick, unsigned short * vendor, unsigned short * product)
{
  CHECK_JS_SOURCE(-1)

  if (jsource->get_usb_ids != NULL)
  {
    return jsource->get_usb_ids(joystick, vendor, product);
  }

  return -1;
}
