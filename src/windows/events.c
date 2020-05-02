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
#include <gimxtime/include/gtime.h>
#include <gimxcommon/include/gperf.h>

GLOG_GET(GLOG_NAME)

#define MOUSE_CAPTURE_RETRY_PERIOD ((gtime)1000000000ULL)

#define NBSAMPLES 1

#define SAMPLETYPE \
    struct { \
            gtime now; \
            gtime delta; \
        }

GPERF_INST(ev_sync_process, SAMPLETYPE, NBSAMPLES);

static int mkb_source = -1;

static struct mkb_source * source_physical = NULL;
static struct mkb_source * source_window = NULL;

static struct mkb_source * mkbsource = NULL;

static struct
{
  HWND hwnd;  // the mouse capture window
  int mode;   // current capture mode
  gtime last; // last call to ev_grab_input
  int status; // 1 if captured, 0 otherwise
} capture = { .hwnd = NULL, .mode = GE_GRAB_OFF, .last = 0, .status = 0 };

static int(*event_callback)(GE_Event*) = NULL;

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

void ev_register_js_source(struct js_source * source)
{
    jsource = source;
}

#define CHECK_JS_SOURCE(RETVAL) \
    do { \
        if (jsource == NULL) { \
            PRINT_ERROR_OTHER("no joystick source available"); \
            return RETVAL; \
        } \
    } while (0)

int ev_init(const GPOLL_INTERFACE * poll_interface __attribute__((unused)), unsigned char mkb_src, int(*callback)(GE_Event*))
{
  mkb_source = mkb_src;

  if (callback == NULL)
  {
    PRINT_ERROR_OTHER("callback is NULL");
    return -1;
  }

  event_callback = callback;

  if (mkb_source == GE_MKB_SOURCE_PHYSICAL)
  {
    mkbsource = source_physical;
    if (mkbsource == NULL)
    {
      PRINT_ERROR_OTHER("no physical mkb source available");
      return -1;
    }
    gpoll_set_rawinput_callback(mkbsource->sync_process);
  }
  else if (mkb_source == GE_MKB_SOURCE_WINDOW_SYSTEM)
  {
    mkbsource = source_window;
    if (mkbsource == NULL)
    {
      PRINT_ERROR_OTHER("no window mkb source available");
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
    PRINT_ERROR_OTHER("no joystick source available");
  }
  else
  {
    if (jsource->init(poll_interface, callback) < 0)
    {
      return -1;
    }
  }

  if(mkb_source == GE_MKB_SOURCE_PHYSICAL)
  {
    capture.hwnd = FindWindow(RAWINPUT_CLASS_NAME, RAWINPUT_WINDOW_NAME);
    if (capture.hwnd == NULL)
    {
      PRINT_ERROR_GETLASTERROR("FindWindow");
      return -1;
    }
  }
  else if(mkb_source == GE_MKB_SOURCE_WINDOW_SYSTEM)
  {
    capture.hwnd = FindWindow(NULL, SDLINPUT_WINDOW_NAME);
    if (capture.hwnd == NULL)
    {
      PRINT_ERROR_GETLASTERROR("FindWindow");
      return -1;
    }
  }

  return 0;
}

void ev_quit(void)
{
  if (capture.mode == GE_GRAB_ON)
  {
    ev_grab_input(GE_GRAB_OFF);
  }

  capture.hwnd = NULL;

  if (mkbsource != NULL)
  {
    mkbsource->quit();
  }

  if (jsource != NULL)
  {
    jsource->quit();
  }

  if (GLOG_LEVEL(GLOG_NAME, DEBUG))
  {
    GPERF_LOG(ev_sync_process);
  }
}

const char* ev_joystick_name(int id)
{
  CHECK_JS_SOURCE(NULL);

  return jsource->get_name(id);
}

int ev_joystick_register(const char* name, unsigned int effects, int (*haptic_cb)(const GE_Event * event))
{
  CHECK_JS_SOURCE(-1);

  return jsource->add(name, effects, haptic_cb);
}

/*
 * Close a joystick, and close the joystick subsystem if none is used anymore.
 */
void ev_joystick_close(int id)
{
  CHECK_JS_SOURCE();

  jsource->close(id);
}

const char* ev_mouse_name(int id)
{
  CHECK_MKB_SOURCE(NULL);

  return mkbsource->get_mouse_name(id);
}

const char* ev_keyboard_name(int id)
{
  CHECK_MKB_SOURCE(NULL);

  return mkbsource->get_keyboard_name(id);
}

static int is_clipped()
{
  if (capture.hwnd == NULL)
  {
    return 0;
  }

  RECT window;
  if (GetWindowRect(capture.hwnd, &window) == 0)
  {
    PRINT_ERROR_GETLASTERROR("GetWindowRect");
    return 0;
  }

  RECT clip;
  if (GetClipCursor(&clip) == 0)
  {
    PRINT_ERROR_GETLASTERROR("GetClipCursor");
    return 0;
  }

  return window.left == clip.left && window.top == clip.top
          && window.right == clip.right && window.bottom == clip.bottom;
}

static int clip()
{
  if (capture.hwnd == NULL)
  {
    return 0;
  }

  RECT window;
  if (GetWindowRect(capture.hwnd, &window) == 0)
  {
    PRINT_ERROR_GETLASTERROR("GetWindowRect");
    return 0;
  }

  if (ClipCursor(&window) == 0)
  {
    PRINT_ERROR_GETLASTERROR("ClipCursor");
    return 0;
  }

  int i = 10;
  while(i > 0 && ShowCursor(FALSE) >= 0) { i--; }

  return 1;
}

static void unclip()
{
  ClipCursor(NULL);

  int i = 10;
  while(i > 0 && ShowCursor(TRUE) < 0) { i--; }
}

int ev_grab_input(int mode)
{
  CHECK_MKB_SOURCE(0);

  if (capture.hwnd == NULL)
  {
    return 0;
  }

  if (mkbsource->grab != NULL && mkbsource->grab(mode))
  {
    return 0;
  }

  capture.mode = mode;
  capture.last = gtime_gettime();

  if(mode == GE_GRAB_ON)
  {
    /*
     * This hack is inspired from:
     *
     * https://docs.microsoft.com/fr-fr/windows/win32/api/winuser/nf-winuser-locksetforegroundwindow#remarks
     *
     *   "The system automatically enables calls to SetForegroundWindow if the user presses the ALT key
     *   or takes some action that causes the system itself to change the foreground window (for example,
     *   clicking a background window)."
     *
     * and it is needed for the following reasons:
     *
     * 1. If we read from stdin before calling ginput_init, the capture window does not reach the foreground,
     * and SetForegroundWindow fails.
     *
     * 2. We can loose focus and cursor clipping due to the following reasons:
     * - a misbehaving application steals our focus, breaking cursor clipping
     * - the display where the capture window is shown changes
     *
     * Previously the hack was to minimize and restore the window to have it come to the foreground,
     * which was not great since it generated extra window events.
     */

    // Press ESC to escape win+tab, ctrl+esc and alt+tab, which can succeed if user smashes both keys at the same time.
    // Press ALT to enable calls to SetForegroundWindow.

    INPUT inputs[] =
    {
      { .type = INPUT_KEYBOARD, .ki = { .wVk = VK_ESCAPE, .dwFlags = 0 } },
      { .type = INPUT_KEYBOARD, .ki = { .wVk = VK_MENU, .dwFlags = 0 } },
    };

    SendInput(sizeof(inputs) / sizeof(*inputs), inputs, sizeof(*inputs));

    if (SetForegroundWindow(capture.hwnd))
    {
      capture.status = clip();
    }
    else
    {
      capture.status = 0;
      PRINT_ERROR_OTHER("failed to set foreground window\n");
    }

    inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(sizeof(inputs) / sizeof(*inputs), inputs, sizeof(*inputs));
  }
  else
  {
    unclip();

    capture.status = 0;
    capture.last = 0;
  }

  return capture.status;
}

#define IS_KEY_DOWN(STATE) (STATE >> 15)

#define ADD_KEY(KEY, FLAGS) \
    do \
    { \
      inputs[input].type = INPUT_KEYBOARD; \
      inputs[input].ki.wVk = KEY; \
      inputs[input].ki.dwFlags = FLAGS; \
      ++input; \
    } while (0)

void ev_sync_process()
{
  if (GLOG_LEVEL(GLOG_NAME, DEBUG))
  {
    GPERF_START(ev_sync_process);
  }

  if (jsource != NULL && jsource->sync_process != NULL)
  {
    jsource->sync_process();
  }
  // on Windows mkbsource->sync_process is either the rawinput callback
  // or NULL (sdlinput)

  if (capture.mode == GE_GRAB_ON)
  {
    // retrieve key states

    uint16_t lwin = GetAsyncKeyState(VK_LWIN);
    uint16_t rwin = GetAsyncKeyState(VK_RWIN);
    uint16_t lalt = GetAsyncKeyState(VK_LMENU);
    uint16_t ralt = GetAsyncKeyState(VK_RMENU);
    uint16_t lctrl = GetAsyncKeyState(VK_LCONTROL);
    uint16_t rctrl = GetAsyncKeyState(VK_RCONTROL);

    int escape = 0;

    // escape windows keys to prevent Search UI to take focus

    if (IS_KEY_DOWN(lwin) || IS_KEY_DOWN(rwin))
    {
      escape = 1;
    }

    INPUT inputs[8] = {};
    unsigned int input = 0;

    if (escape)
    {
      ADD_KEY(VK_ESCAPE, 0);
      ADD_KEY(VK_ESCAPE, KEYEVENTF_KEYUP);
    }

    // release keys used in windows shortcuts

    if (IS_KEY_DOWN(lwin))
    {
      ADD_KEY(VK_LWIN, KEYEVENTF_KEYUP);
    }

    if (IS_KEY_DOWN(rwin))
    {
      ADD_KEY(VK_RWIN, KEYEVENTF_KEYUP);
    }

    if (IS_KEY_DOWN(lalt))
    {
      ADD_KEY(VK_LMENU, KEYEVENTF_KEYUP);
    }

    if (IS_KEY_DOWN(ralt))
    {
      ADD_KEY(VK_RMENU, KEYEVENTF_KEYUP);
    }

    if (IS_KEY_DOWN(lctrl))
    {
      ADD_KEY(VK_LCONTROL, KEYEVENTF_KEYUP);
    }

    if (IS_KEY_DOWN(rctrl))
    {
      ADD_KEY(VK_RCONTROL, KEYEVENTF_KEYUP);
    }

    // if you add more entries to inputs, make sure not to increase its size!

    SendInput(input, inputs, sizeof(*inputs));

    // check if mouse capture is still valid as it may fail in a few cases

    if (capture.status)
    {
      capture.status = is_clipped();
      if (!capture.status)
      {
        if (GLOG_LEVEL(GLOG_NAME, INFO))
        {
          printf("mouse capture broke\n");
        }
      }
    }
    if (capture.status == 0 && gtime_gettime() - capture.last >= MOUSE_CAPTURE_RETRY_PERIOD)
    {
      ev_grab_input(GE_GRAB_ON);
      if (GLOG_LEVEL(GLOG_NAME, INFO))
      {
        if (capture.status)
        {
          printf("mouse capture succeeded\n");
        }
        else
        {
          printf("mouse capture failed\n");
        }
      }
    }
  }

  if (GLOG_LEVEL(GLOG_NAME, DEBUG))
  {
    GPERF_END(ev_sync_process);
  }
}

int ev_joystick_get_haptic(int joystick)
{
  CHECK_JS_SOURCE(-1);

  if (jsource->get_haptic != NULL)
  {
    return jsource->get_haptic(joystick);
  }

  return -1;
}

int ev_joystick_set_haptic(const GE_Event * event)
{
  CHECK_JS_SOURCE(-1);

  if (jsource->set_haptic != NULL)
  {
    return jsource->set_haptic(event);
  }
  
  return -1;
}

int ev_joystick_get_usb_ids(int joystick, unsigned short * vendor, unsigned short * product)
{
  CHECK_JS_SOURCE(-1);

  if (jsource->get_usb_ids != NULL)
  {
    return jsource->get_usb_ids(joystick, vendor, product);
  }

  return -1;
}
