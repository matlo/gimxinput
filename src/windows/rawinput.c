/*
  Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
  License: GPLv3

  This code is derived from the manymouse library: https://icculus.org/manymouse/
  Original licence:

  Copyright (c) 2005-2012 Ryan C. Gordon and others.

  This software is provided 'as-is', without any express or implied warranty.
  In no event will the authors be held liable for any damages arising from
  the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
  claim that you wrote the original software. If you use this software in a
  product, an acknowledgment in the product documentation would be
  appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not be
  misrepresented as being the original software.

  3. This notice may not be removed or altered from any source distribution.

     Ryan C. Gordon <icculus@icculus.org>
 */

#include "scancodes.h"
#include "../events.h"

/* WinUser.h won't include rawinput stuff without this... */
#if (_WIN32_WINNT < 0x0501)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <setupapi.h>

#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>

#define MAX_DEVICES 256
#define MAX_KEYS 256

#define HID_USAGE_PAGE_GENERIC 1
#define HID_USAGE_GENERIC_MOUSE 2
#define HID_USAGE_GENERIC_KEYBOARD 6

#define RI_MOUSE_HWHEEL 0x0800

#define RAWINPUT_MAX_EVENTS 1024

GLOG_GET(GLOG_NAME)

static HWND raw_hwnd = NULL;
static const char * class_name = RAWINPUT_CLASS_NAME;
static const char * win_name = RAWINPUT_WINDOW_NAME;
static ATOM class_atom = 0;

static struct {
    HANDLE handle;
    char * name;
} * mice = NULL;

static unsigned int nb_mice = 0;

static struct {
    HANDLE handle;
    char * name;
    unsigned char * keystates;
} * keyboards = NULL;

static unsigned int nb_keyboards = 0;

static int registered = 0;

static int (*event_callback)(GE_Event*) = NULL;

static int pollres = 0;

#define PROCESS_EVENT(EVT) \
    do { \
      int res = event_callback(&EVT); \
      pollres |= res; \
    } while (0)

static void rawinput_handler(const RAWINPUT * raw, UINT align) {

    unsigned int device;
    const RAWINPUTHEADER * header = &raw->header;
    const RAWMOUSE * mouse = (void*)&raw->data.mouse + align;
    const RAWKEYBOARD * keyboard = (void*)&raw->data.keyboard + align;
    GE_Event event = {};
    UINT scanCode;

    if (raw->header.dwType == RIM_TYPEMOUSE) {

      for (device = 0; device < nb_mice; ++device) {
        if (mice[device].handle == header->hDevice) {
          break;
        }
      }

      if (device == nb_mice) {
        return;
      }

      if (mouse->usFlags == MOUSE_MOVE_RELATIVE) {
          event.type = GE_MOUSEMOTION;
          event.motion.which = device;
          if (mouse->lLastX != 0) {
            event.motion.xrel = mouse->lLastX;
            event.motion.yrel = 0;
            PROCESS_EVENT(event);
          }
          if (mouse->lLastY != 0) {
            event.motion.xrel = 0;
            event.motion.yrel = mouse->lLastY;
            PROCESS_EVENT(event);
          }
      }

      #define QUEUE_BUTTON(ID,BUTTON) { \
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_##ID##_DOWN) { \
          event.type = GE_MOUSEBUTTONDOWN; \
          event.button.which = device; \
          event.button.button = BUTTON; \
          PROCESS_EVENT(event); \
        } \
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_##ID##_UP) { \
          event.type = GE_MOUSEBUTTONUP; \
          event.button.which = device; \
          event.button.button = BUTTON; \
          PROCESS_EVENT(event); \
        } \
      }

      QUEUE_BUTTON(1, GE_BTN_LEFT)
      QUEUE_BUTTON(2, GE_BTN_RIGHT)
      QUEUE_BUTTON(3, GE_BTN_MIDDLE)
      QUEUE_BUTTON(4, GE_BTN_BACK)
      QUEUE_BUTTON(5, GE_BTN_FORWARD)

      #undef QUEUE_BUTTON
      
      #define QUEUE_WHEEL_BUTTON(WHEEL,BUTTON_PLUS,BUTTON_MINUS) { \
        if (mouse->usButtonFlags & RI_MOUSE_##WHEEL) { \
          if (mouse->usButtonData != 0) { \
            event.type = GE_MOUSEBUTTONDOWN; \
            event.button.which = device; \
            event.button.button = ((SHORT) mouse->usButtonData) > 0 ? BUTTON_PLUS : BUTTON_MINUS; \
            PROCESS_EVENT(event); \
            event.type = GE_MOUSEBUTTONUP; \
            PROCESS_EVENT(event); \
          } \
        } \
      }\

      QUEUE_WHEEL_BUTTON(WHEEL, GE_BTN_WHEELUP, GE_BTN_WHEELDOWN)
      QUEUE_WHEEL_BUTTON(HWHEEL, GE_BTN_WHEELRIGHT, GE_BTN_WHEELLEFT)
      
      #undef QUEUE_WHEEL_BUTTON

    } else if(raw->header.dwType == RIM_TYPEKEYBOARD) {

      for (device = 0; device < nb_keyboards; device++) {
        if (keyboards[device].handle == header->hDevice) {
          break;
        }
      }

      if (device == nb_keyboards) {
        return;
      }
      
      scanCode = get_keycode(keyboard->Flags, keyboard->MakeCode);
      if (scanCode == 0) {
        return;
      }
            
      if(keyboard->Flags & RI_KEY_BREAK) {
        keyboards[device].keystates[scanCode] = 0;
      } else if(keyboards[device].keystates[scanCode] == 0) {
        keyboards[device].keystates[scanCode] = 1;
      } else {
        return;
      }
    
      event.key.which = device;
      event.key.keysym = scanCode;
      if(keyboard->Flags & RI_KEY_BREAK) {
        event.key.type = GE_KEYUP;
      } else {
        event.key.type = GE_KEYDOWN;
      }
      PROCESS_EVENT(event);

    } else {
      return;
    }
}


static void wminput_handler(WPARAM wParam __attribute__((unused)), LPARAM lParam)
{
  UINT dwSize = 0;

  GetRawInputData((HRAWINPUT) lParam, RID_INPUT, NULL, &dwSize, sizeof (RAWINPUTHEADER));

  if (dwSize == 0) {
    return;
  }

  LPBYTE lpb[dwSize];

  if (GetRawInputData((HRAWINPUT) lParam, RID_INPUT, lpb, &dwSize, sizeof (RAWINPUTHEADER)) != dwSize) {
      return;
  }

  rawinput_handler((RAWINPUT *) lpb, 0);
}

BOOL bIsWow64 = FALSE;

void wminput_handler_buff()
{
  UINT i;
  static RAWINPUT RawInputs[RAWINPUT_MAX_EVENTS];
  UINT cbSize = sizeof(RawInputs);
      
  UINT nInput = GetRawInputBuffer(RawInputs, &cbSize, sizeof(RAWINPUTHEADER));
  
  if (nInput == (UINT)-1) {
    return;
  }
  
  for (i = 0; i < nInput; ++i) {
    if(bIsWow64) {
      rawinput_handler((RAWINPUT *) RawInputs + i, 8);
    } else {
      rawinput_handler((RAWINPUT *) RawInputs + i, 0);
    }
  }
}

/*
 * For some reason GetRawInputBuffer does not work in Windows 8:
 * it seems there remain WM_INPUT messages in the queue, which
 * make the MsgWaitForMultipleInput always return immediately.
 */
int buff = 0;

static LRESULT CALLBACK RawWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {

    if (Msg == WM_DESTROY) {
        return 0;
    }

    if (Msg == WM_INPUT) {
        if(!buff) {
            wminput_handler(wParam, lParam);
        }
    }

    return DefWindowProc(hWnd, Msg, wParam, lParam);
}

static int register_raw_input(int state) {

    if (registered == state) {
      return 0;
    }

    DWORD dwFlags = state ? (RIDEV_NOLEGACY | RIDEV_INPUTSINK) : RIDEV_REMOVE;
    HWND hwndTarget = state ? raw_hwnd : 0;

    RAWINPUTDEVICE rid[] = {
      {
        .usUsagePage = HID_USAGE_PAGE_GENERIC,
        .usUsage = HID_USAGE_GENERIC_MOUSE,
        .dwFlags = dwFlags,
        .hwndTarget = hwndTarget
      },
      {
        .usUsagePage = HID_USAGE_PAGE_GENERIC,
        .usUsage = HID_USAGE_GENERIC_KEYBOARD,
        .dwFlags = dwFlags,
        .hwndTarget = hwndTarget
      }
    };

    if (RegisterRawInputDevices(rid, sizeof(rid) / sizeof(*rid), sizeof(*rid)) == FALSE) {
        PRINT_ERROR_GETLASTERROR("RegisterRawInputDevices");
        return -1;
    }

    registered = state;

    return 0;
}

static int init_event_queue(void)
{
    HANDLE hInstance = GetModuleHandle(NULL);
    
    WNDCLASSEX wce = {
      .cbSize = sizeof(WNDCLASSEX),
      .lpfnWndProc = RawWndProc,
      .lpszClassName = class_name,
      .hInstance = hInstance,
    };
    class_atom = RegisterClassEx(&wce);
    if (class_atom == 0)
        return 0;
    
    //create the window at the position of the cursor
    POINT cursor_pos;
    GetCursorPos(&cursor_pos);
    
    // mouse capture is broken with a 1x1 window and "fix scaling for apps" enabled
    raw_hwnd = CreateWindow(class_name, win_name, WS_POPUP | WS_VISIBLE | WS_SYSMENU, cursor_pos.x, cursor_pos.y, 2, 2, NULL, NULL, hInstance, NULL);

    if (raw_hwnd == NULL) {
      PRINT_ERROR_GETLASTERROR("CreateWindow");
      return 0;
    }

    if (register_raw_input(1) < 0) {
      return 0;
    }
    
    ShowWindow(raw_hwnd, SW_SHOW);

    return 1;
}

static void cleanup_window(void) {

  if (raw_hwnd) {
      MSG Msg;
      DestroyWindow(raw_hwnd);
      while (PeekMessage(&Msg, raw_hwnd, 0, 0, PM_REMOVE)) {
          TranslateMessage(&Msg);
          DispatchMessage(&Msg);
      }
      raw_hwnd = 0;
  }

  if (class_atom) {
      UnregisterClass(class_name, GetModuleHandle(NULL));
      class_atom = 0;
  }
}

static struct {
  char * instanceId;
  SP_DEVINFO_DATA data;
} * devinfos = NULL;

static unsigned int nb_devinfos = 0;

static HDEVINFO hdevinfo = INVALID_HANDLE_VALUE;

static int get_devinfos() {
  
  const DWORD flags = DIGCF_ALLCLASSES | DIGCF_PRESENT;
  hdevinfo = SetupDiGetClassDevs(NULL, NULL, NULL, flags);
  if (hdevinfo == INVALID_HANDLE_VALUE) {
    PRINT_ERROR_GETLASTERROR("SetupDiGetClassDevs");
    return -1;
  }
  
  DWORD i = 0;
  while (1) {
    SP_DEVINFO_DATA data = { .cbSize = sizeof(SP_DEVINFO_DATA) };
    BOOL result = SetupDiEnumDeviceInfo(hdevinfo, i++, &data);
    if (result == TRUE) {
      DWORD bufsize;
      result = SetupDiGetDeviceInstanceId(hdevinfo, &data, NULL, 0, &bufsize);
      if (result == FALSE && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        char * buf = malloc(bufsize);
        if (buf == NULL) {
          PRINT_ERROR_ALLOC_FAILED("malloc");
          continue;
        }
        result = SetupDiGetDeviceInstanceId(hdevinfo, &data, buf, bufsize, NULL);
        if (result == TRUE) {
          void * ptr = realloc(devinfos, (nb_devinfos + 1) * sizeof(*devinfos));
          if (ptr == NULL) {
            PRINT_ERROR_ALLOC_FAILED("malloc");
            free(buf);
            continue;
          }
          devinfos = ptr;
          devinfos[nb_devinfos].instanceId = buf;
          devinfos[nb_devinfos].data = data;
          ++nb_devinfos;
        } else {
          PRINT_ERROR_GETLASTERROR("SetupDiGetDeviceInstanceId");
          free(buf);
        }
      } else {
        PRINT_ERROR_GETLASTERROR("SetupDiGetDeviceInstanceId");
      }
    } else if (GetLastError() == ERROR_NO_MORE_ITEMS) {
      break;
    } else {
      PRINT_ERROR_GETLASTERROR("SetupDiEnumDeviceInfo");
    }
  }

  return 0;
}

void free_dev_info() {
  if (hdevinfo != INVALID_HANDLE_VALUE) {
    SetupDiDestroyDeviceInfoList(hdevinfo);
  }
  unsigned int i;
  for (i = 0; i < nb_devinfos; ++i) {
    free(devinfos[i].instanceId);
  }
  free(devinfos);
  devinfos = NULL;
  nb_devinfos = 0;
}

static SP_DEVINFO_DATA * get_devinfo_data(const char * instanceId) {
  
  unsigned int i;
  for (i = 0; i < nb_devinfos; ++i) {
    if (strcasecmp(instanceId, devinfos[i].instanceId) == 0) {
      return &devinfos[i].data;
    }
  }
  return NULL;
}

/*
 * Convert an UTF-16LE string to an UTF-8 string.
 */
static char * utf16le_to_utf8(const unsigned char * inbuf)
{
  char * outbuf = NULL;
  int outsize = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR ) inbuf, -1, NULL, 0, NULL, NULL);
  if (outsize != 0) {
      outbuf = (char*) malloc(outsize);
      if (outbuf != NULL) {
         int res = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR ) inbuf, -1, outbuf, outsize, NULL, NULL);
         if (res == 0) {
             PRINT_ERROR_GETLASTERROR("WideCharToMultiByte");
             free(outbuf);
             outbuf = NULL;
         }
      }
  } else {
    PRINT_ERROR_GETLASTERROR("WideCharToMultiByte");
  }

  return outbuf;
}

static char * get_dev_name_by_instance(const char * devinstance) {

  SP_DEVINFO_DATA * devdata = get_devinfo_data(devinstance);
  if (devdata != NULL) {
    DWORD size;
    BOOL result = SetupDiGetDeviceRegistryPropertyW(hdevinfo, devdata, SPDRP_DEVICEDESC, NULL, NULL, 0, &size);
    if (result == FALSE && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      unsigned char desc[size];
      result = SetupDiGetDeviceRegistryPropertyW(hdevinfo, devdata, SPDRP_DEVICEDESC, NULL, desc, size, NULL);
      if (result == FALSE) {
        PRINT_ERROR_GETLASTERROR("SetupDiGetDeviceRegistryProperty");
      } else {
        return utf16le_to_utf8(desc);
      }
    } else {
      PRINT_ERROR_GETLASTERROR("SetupDiGetDeviceRegistryProperty");
    }
  }
  
  return NULL;
}

static void init_device(const RAWINPUTDEVICELIST * dev) {

  if (dev->dwType != RIM_TYPEMOUSE && dev->dwType != RIM_TYPEKEYBOARD) {
    return;
  }

  UINT count = 0;
  if (GetRawInputDeviceInfo(dev->hDevice, RIDI_DEVICENAME, NULL, &count) == (UINT)-1) {
    PRINT_ERROR_GETLASTERROR("GetRawInputDeviceInfo");
    return;
  }

  char abuf[count + 1];
  memset(abuf, 0x00, sizeof(abuf));

  char * buf = abuf;

  if (GetRawInputDeviceInfo(dev->hDevice, RIDI_DEVICENAME, buf, &count) == (UINT)-1) {
    PRINT_ERROR_GETLASTERROR("GetRawInputDeviceInfo");
    return;
  }
  
  // skip remote desktop devices
  if (strstr(buf, "Root#RDP_")) {
    return;
  }

  // XP starts these strings with "\\??\\" ... Vista does "\\\\?\\".  :/
  while ((*buf == '?') || (*buf == '\\')) {
    buf++;
    count--;
  }

  // get the device instance id
  char * ptr;
  for (ptr = buf; *ptr; ptr++) {
    if (*ptr == '#') {
      *ptr = '\\'; // convert '#' to '\\'
    } else if (*ptr == '{') { // GUID part
      if (*(ptr-1) == '\\') {
        ptr--;
      }
      break;
    }
  }
  *ptr = '\0';
  
  if (dev->dwType == RIM_TYPEMOUSE) {
    char * name = get_dev_name_by_instance(buf);
    if (name == NULL) {
      return;
    }
    void * ptr = realloc(mice, (nb_mice + 1) * sizeof(*mice));
    if (ptr == NULL) {
      PRINT_ERROR_ALLOC_FAILED("realloc");
      free(name);
      return;
    }
    mice = ptr;
    mice[nb_mice].name = name;
    mice[nb_mice].handle = dev->hDevice;
    nb_mice++;
  }
  else if(dev->dwType == RIM_TYPEKEYBOARD) {
    char * name = get_dev_name_by_instance(buf);
    if (name == NULL) {
      return;
    }
    unsigned char * keystates = calloc(MAX_KEYS, sizeof(*keystates));
    if (keystates == NULL) {
      PRINT_ERROR_ALLOC_FAILED("calloc");
      free(name);
      return;
    }
    void * ptr = realloc(keyboards, (nb_keyboards + 1) * sizeof(*keyboards));
    if (ptr == NULL) {
      PRINT_ERROR_ALLOC_FAILED("realloc");
      free(keystates);
      free(name);
      return;
    }
    keyboards = ptr;
    keyboards[nb_keyboards].name = name;
    keyboards[nb_keyboards].handle = dev->hDevice;
    keyboards[nb_keyboards].keystates = keystates;
    nb_keyboards++;
  }
}

void rawinput_quit(void) {

  register_raw_input(0);
  cleanup_window();
  unsigned int i;
  for(i = 0; i < nb_keyboards; ++i) {
    free(keyboards[i].name);
    free(keyboards[i].keystates);
  }
  free(keyboards);
  keyboards = NULL;
  nb_keyboards = 0;
  for(i = 0; i < nb_mice; ++i) {
    free(mice[i].name);
  }
  free(mice);
  mice = NULL;
  nb_mice = 0;
}

int rawinput_init(const GPOLL_INTERFACE * poll_interface __attribute__((unused)), int (*callback)(GE_Event*)) {

  if (callback == NULL) {
    PRINT_ERROR_OTHER("callback is NULL");
    return -1;
  }

  event_callback = callback;
  
  if (get_devinfos() < 0) {
    return -1;
  }

  UINT count = 0;
  UINT result = GetRawInputDeviceList(NULL, &count, sizeof(RAWINPUTDEVICELIST));
  if (result == (UINT)-1) {
    PRINT_ERROR_GETLASTERROR("GetRawInputDeviceList");
  } else if (count > 0) {
    RAWINPUTDEVICELIST * devlist = (PRAWINPUTDEVICELIST) malloc(count * sizeof(RAWINPUTDEVICELIST));
    result = GetRawInputDeviceList(devlist, &count, sizeof(RAWINPUTDEVICELIST));
    if (result != (UINT)-1) {
      unsigned int i;
      for (i = 0; i < result; i++) { // result may be lower than count!
        init_device(&devlist[i]);
      }
    }
    free(devlist);
  }
  
  free_dev_info();
  
  if(result == (UINT)-1) {
    rawinput_quit();
    return -1;
  }
  
  if (!init_event_queue()) {
    rawinput_quit();
    return -1;
  }
  
  IsWow64Process(GetCurrentProcess(), &bIsWow64);

  return 0;
}

const char * rawinput_mouse_name(int index) {
  return ((unsigned int) index < nb_mice) ? mice[index].name : NULL;
}

const char * rawinput_keyboard_name(int index) {
  return ((unsigned int) index < nb_keyboards) ? keyboards[index].name : NULL;
}

int rawinput_poll() {

  MSG Msg;
  
  if(buff) {

    /* process WM_INPUT events */
    wminput_handler_buff();
    
    /* process all other events, otherwise the message queue quickly gets full */
    while (PeekMessage(&Msg, raw_hwnd, 0, WM_INPUT-1, PM_REMOVE)) {
        DefWindowProc(Msg.hwnd, Msg.message, Msg.wParam, Msg.lParam);
    }
    while (PeekMessage(&Msg, raw_hwnd, WM_INPUT+1, 0xFFFF, PM_REMOVE)) {
        DefWindowProc(Msg.hwnd, Msg.message, Msg.wParam, Msg.lParam);
    }
  } else {

    /* process all events */
    while (PeekMessage(&Msg, raw_hwnd, 0, 0, PM_REMOVE)) {
      TranslateMessage(&Msg);
      /* process messages including WM_INPUT ones */
      DispatchMessage(&Msg);
    }
  }

  int ret = pollres;

  pollres = 0;
  
  return ret;
}

static int rawinput_get_src() {

    return GE_MKB_SOURCE_PHYSICAL;
}

static struct mkb_source rawinput_source = {
    .init = rawinput_init,
    .get_src = rawinput_get_src,
    .grab = NULL,
    .get_mouse_name = rawinput_mouse_name,
    .get_keyboard_name = rawinput_keyboard_name,
    .sync_process = rawinput_poll,
    .quit = rawinput_quit,
};

void mkb_constructor() __attribute__((constructor));
void mkb_constructor() {
    ev_register_mkb_source(&rawinput_source);
}
