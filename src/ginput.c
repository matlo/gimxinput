/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <ginput.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "conversion.h"
#include "events.h"
#include "queue.h"
#ifndef WIN32
#include <poll.h>
#else
#include <windows.h>
#endif
#include "hid/hidinput.h"
#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>

#ifdef __linux__
#define SIXAXIS_NAME "Sony PLAYSTATION(R)3 Controller"
#define BT_SIXAXIS_NAME "PLAYSTATION(R)3 Controller"
#define DUALSHOCK4_NAME "Sony Computer Entertainment Wireless Controller"
#define DUALSHOCK4_V2_NAME "Sony Interactive Entertainment Wireless Controller"

#define XBOX_CONTROLLER_NAME "Microsoft X-Box One pad"
#define XBOX_CONTROLLER_V2_NAME "Microsoft X-Box One pad (Firmware 2015)"
#define XBOX_CONTROLLER_V3_NAME "Microsoft X-Box One S pad"
#endif

GLOG_INST(GLOG_NAME)

static struct
{
  char* name;
  int virtualIndex;
  unsigned char isUsed;
} joysticks[GE_MAX_DEVICES] = {};

static struct
{
  char* name;
  int virtualIndex;
} mice[GE_MAX_DEVICES] = {};

static struct
{
  char* name;
  int virtualIndex;
} keyboards[GE_MAX_DEVICES] = {};

static int grab = GE_GRAB_OFF;

static GE_MK_Mode mk_mode = GE_MK_MODE_MULTIPLE_INPUTS;

static int initialized = 0;

static void get_joysticks()
{
  const char* name;
  int j;
  int i = 0;
  while (i < GE_MAX_DEVICES && (name = ev_joystick_name(i)))
  {
#ifdef __linux__
    if (!strncmp(name, BT_SIXAXIS_NAME, sizeof(BT_SIXAXIS_NAME) - 1))
    {
      // Rename QtSixA devices.
      name = SIXAXIS_NAME;
    }
    else if (!strncmp(name, DUALSHOCK4_V2_NAME, sizeof(DUALSHOCK4_V2_NAME)))
    {
      // Rename Dualshock 4 v2.
      name = DUALSHOCK4_NAME;
    }
    else if (!strncmp(name, XBOX_CONTROLLER_V2_NAME, sizeof(XBOX_CONTROLLER_V2_NAME)))
    {
      name = XBOX_CONTROLLER_NAME;
    }
    else if (!strncmp(name, XBOX_CONTROLLER_V3_NAME, sizeof(XBOX_CONTROLLER_V3_NAME)))
    {
      name = XBOX_CONTROLLER_NAME;
    }
#endif

    joysticks[i].name = strdup(name);

    // Go backward and look for a joystick with the same name.
    for (j = i - 1; j >= 0; --j)
    {
      if (!strcmp(joysticks[i].name, joysticks[j].name))
      {
        // Found => compute the virtual index.
        joysticks[i].virtualIndex = joysticks[j].virtualIndex + 1;
        break;
      }
    }
    if (j < 0)
    {
      // Not found => the virtual index is 0.
      joysticks[i].virtualIndex = 0;
    }
    i++;
  }
}

static void get_mkbs()
{
  const char* name;
  int j;
  int i = 0;
  while (i < GE_MAX_DEVICES && (name = ev_mouse_name(i)))
  {
    mice[i].name = strdup(name);

    // Go backward and look for a mouse with the same name.
    for (j = i - 1; j >= 0; --j)
    {
      if (!strcmp(mice[i].name, mice[j].name))
      {
        // Found => compute the virtual index.
        mice[i].virtualIndex = mice[j].virtualIndex + 1;
        break;
      }
    }
    if (j < 0)
    {
      // Not found => the virtual index is 0.
      mice[i].virtualIndex = 0;
    }
    i++;
  }
  i = 0;
  while (i < GE_MAX_DEVICES && (name = ev_keyboard_name(i)))
  {
    keyboards[i].name = strdup(name);

    // Go backward and look for a keyboard with the same name.
    for (j = i - 1; j >= 0; --j)
    {
      if (!strcmp(keyboards[i].name, keyboards[j].name))
      {
        // Found => compute the virtual index.
        keyboards[i].virtualIndex = keyboards[j].virtualIndex + 1;
        break;
      }
    }
    if (j < 0)
    {
      // Not found => the virtual index is 0.
      keyboards[i].virtualIndex = 0;
    }
    i++;
  }
}

int ginput_init(const GPOLL_INTERFACE * poll_interface, unsigned char mkb_src, int(*callback)(GE_Event*))
{
  if (hidinput_init(poll_interface, callback) < 0)
  {
      return -1;
  }

  if (ev_init(poll_interface, mkb_src, callback) < 0)
  {
    return -1;
  }

  get_joysticks();

  if (mkb_src != GE_MKB_SOURCE_NONE)
  {
    get_mkbs();
  }

  queue_init();

  initialized = 1;

  return 0;
}

void ginput_release_unused()
{
  int i;
  for (i = 0; i < GE_MAX_DEVICES && joysticks[i].name; ++i)
  {
    if (!joysticks[i].isUsed)
    {
      free(joysticks[i].name);
      joysticks[i].name = NULL;
      ev_joystick_close(i);
    }
  }
}

int ginput_grab_toggle()
{
  grab = ev_grab_input(grab ? GE_GRAB_OFF : GE_GRAB_ON);

  return grab;
}

void ginput_grab()
{
  ev_grab_input(GE_GRAB_ON);
  grab = 1;
}

void ginput_free_mk_names()
{
  int i;
  for (i = 0; i < GE_MAX_DEVICES && mice[i].name; ++i)
  {
    free(mice[i].name);
    mice[i].name = NULL;
  }
  for (i = 0; i < GE_MAX_DEVICES && keyboards[i].name; ++i)
  {
    free(keyboards[i].name);
    keyboards[i].name = NULL;
  }
}

void ginput_quit()
{
  int i;

  for (i = 0; i < GE_MAX_DEVICES; ++i)
  {
    if (joysticks[i].name)
    {
      free(joysticks[i].name);
      joysticks[i].name = NULL;
      ev_joystick_close(i);
    }
  }
  ginput_free_mk_names();
  ev_quit();

  hidinput_quit();

  initialized = 0;
}

const char * ginput_mouse_name(int id)
{
  if (id >= 0 && id < GE_MAX_DEVICES)
  {
    return mice[id].name;
  }
  return NULL;
}

const char * ginput_keyboard_name(int id)
{
  if (id >= 0 && id < GE_MAX_DEVICES)
  {
    return keyboards[id].name;
  }
  return NULL;
}

const char * ginput_joystick_name(int id)
{
  if (id >= 0 && id < GE_MAX_DEVICES)
  {
    return joysticks[id].name;
  }
  return NULL;
}

int ginput_joystick_virtual_id(int id)
{
  if (id >= 0 && id < GE_MAX_DEVICES)
  {
    return joysticks[id].virtualIndex;
  }
  return 0;
}

void ginput_set_joystick_used(int id)
{
  if (id >= 0 && id < GE_MAX_DEVICES)
  {
    joysticks[id].isUsed = 1;
  }
}

int ginput_register_joystick(const char* name, unsigned int effects, int (*haptic_cb)(const GE_Event * event))
{
  if(initialized)
  {
    PRINT_ERROR_OTHER("this function can only be called before ginput_init");
    return -1;
  }

  return ev_joystick_register(name, effects, haptic_cb);
}

int ginput_mouse_virtual_id(int id)
{
  if (id >= 0 && id < GE_MAX_DEVICES)
  {
    return mice[id].virtualIndex;
  }
  return 0;
}

int ginput_keyboard_virtual_id(int id)
{
  if (id >= 0 && id < GE_MAX_DEVICES)
  {
    return keyboards[id].virtualIndex;
  }
  return 0;
}

GE_MK_Mode ginput_get_mk_mode()
{
  return mk_mode;
}

void ginput_set_mk_mode(GE_MK_Mode value)
{
  mk_mode = value;
}

int ginput_get_device_id(GE_Event* e)
{
  /*
   * 'which' should always be at that place
   * There is no need to check the value, since it's stored as an uint8_t, and GE_MAX_DEVICES is 256.
   */
  unsigned int device_id = ((GE_KeyboardEvent*) e)->which;

  switch (e->type)
  {
    case GE_JOYHATMOTION:
    case GE_JOYBUTTONDOWN:
    case GE_JOYBUTTONUP:
    case GE_JOYAXISMOTION:
      break;
    case GE_KEYDOWN:
    case GE_KEYUP:
    case GE_MOUSEBUTTONDOWN:
    case GE_MOUSEBUTTONUP:
    case GE_MOUSEMOTION:
      if (ginput_get_mk_mode() == GE_MK_MODE_SINGLE_INPUT)
      {
        device_id = 0;
      }
      break;
  }

  return device_id;
}

int ginput_queue_push(GE_Event *event)
{
  return queue_push_event(event);
}

int ginput_joystick_get_haptic(int id)
{
  return ev_joystick_get_haptic(id);
}

int ginput_joystick_set_haptic(const GE_Event * event)
{
  return ev_joystick_set_haptic(event);
}

#ifndef WIN32
void * ginput_joystick_get_hid(int id)
{
  return ev_joystick_get_hid(id);
}

int ginput_joystick_set_hid_callbacks(void * dev, void * user, int (* hid_write_cb)(void * user, int transfered), int (* hid_close_cb)(void * user))
{
  return hidinput_set_callbacks(dev, user, hid_write_cb, hid_close_cb);
}
#else
int ginput_joystick_get_usb_ids(int id, unsigned short * vendor, unsigned short * product)
{
  return ev_joystick_get_usb_ids(id, vendor, product);
}
#endif

void ginput_periodic_task()
{
  ev_sync_process();
  hidinput_poll();
}

int ginput_queue_pop(GE_Event *events, int numevents)
{
  return queue_pop_events(events, numevents);
}

const char* ginput_mouse_button_name(int button)
{
  return get_chars_from_button(button);
}

int ginput_mouse_button_id(const char* name)
{
  return get_mouse_event_id_from_buffer(name);
}

const char* ginput_key_name(uint16_t key)
{
  return get_chars_from_key(key);
}

uint16_t ginput_key_id(const char* name)
{
  return get_key_from_buffer(name);
}

