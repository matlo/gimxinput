/*
 Copyright (c) 2018 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "scancodes.h"
#include "../events.h"

#include <windows.h>
#include <windowsx.h>

#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>

#define MAX_KEYS 256

GLOG_GET(GLOG_NAME)

static int (*event_callback)(GE_Event*) = NULL;

static unsigned char keystates[MAX_KEYS] = { };

#define WHEEL_DELTA 120
#define WM_MOUSEHWHEEL 0x020E

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {

    if (nCode == HC_ACTION) {
        PMSLLHOOKSTRUCT p = (PMSLLHOOKSTRUCT)lParam;
        GE_Event event = {};
        POINT pos;
        switch (wParam) {
        case WM_LBUTTONDOWN:
            event.type = GE_MOUSEBUTTONDOWN;
            event.button.button = GE_BTN_LEFT;
            event_callback(&event);
            break;
        case WM_LBUTTONUP:
            event.type = GE_MOUSEBUTTONUP;
            event.button.button = GE_BTN_LEFT;
            event_callback(&event);
            break;
        case WM_MOUSEMOVE:
        {
        	// get relative cursor position substracting current cursor position
            GetCursorPos(&pos);
            event.type = GE_MOUSEMOTION;
            event.motion.xrel = p->pt.x - pos.x;
            event.motion.yrel = 0;
            event_callback(&event);
            event.motion.xrel = 0;
            event.motion.yrel = p->pt.y - pos.y;
            event_callback(&event);
            break;
        }
        case WM_MOUSEWHEEL:
            event.type = GE_MOUSEBUTTONDOWN;
            event.button.button = ((SHORT) HIWORD(p->mouseData)) > 0 ? GE_BTN_WHEELUP : GE_BTN_WHEELDOWN;
            event_callback(&event);
            event.type = GE_MOUSEBUTTONUP;
            event_callback(&event);
            break;
        case WM_MOUSEHWHEEL:
            event.type = GE_MOUSEBUTTONDOWN;
            event.button.button = ((SHORT) HIWORD(p->mouseData)) > 0 ? GE_BTN_WHEELRIGHT : GE_BTN_WHEELLEFT;
            event_callback(&event);
            event.type = GE_MOUSEBUTTONUP;
            event_callback(&event);
            break;
        case WM_RBUTTONDOWN:
            event.type = GE_MOUSEBUTTONDOWN;
            event.button.button = GE_BTN_RIGHT;
            event_callback(&event);
            break;
        case WM_RBUTTONUP:
            event.type = GE_MOUSEBUTTONUP;
            event.button.button = GE_BTN_RIGHT;
            event_callback(&event);
            break;
        case WM_XBUTTONDOWN:
            event.type = GE_MOUSEBUTTONDOWN;
            event.button.button = (HIWORD(p->mouseData) == 0x0001) ? GE_BTN_BACK : GE_BTN_FORWARD;
            event_callback(&event);
            break;
        case WM_XBUTTONUP:
            event.type = GE_MOUSEBUTTONUP;
            event.button.button = (HIWORD(p->mouseData) == 0x0001) ? GE_BTN_BACK : GE_BTN_FORWARD;
            event_callback(&event);
            break;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {

    if (nCode == HC_ACTION) {
        PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT) lParam;
        GE_Event event = { };
        unsigned short flags = 0;
        unsigned short scanCode = p->scanCode;
        if (p->flags & 0x01) {
            flags = RI_KEY_E0;
        }
        if (scanCode == 0x45) {
            if (!(p->flags & 0x01)) {
                scanCode = 0x1D;
                flags = RI_KEY_E1;
            } else {
                flags = 0;
            }
        }
        // fprintf(stderr, "%d %08x %08x %08x %08x\n", wParam, p->scanCode, p->flags, scanCode, flags); fflush(stderr);
        UINT keyCode = get_keycode(flags, scanCode);
        if (keyCode != 0) {
            switch (wParam) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (keystates[p->scanCode] == 0) {
                    event.key.type = GE_KEYDOWN;
                    event.key.keysym = keyCode;
                    event_callback(&event);
                    keystates[p->scanCode] = 1;
                }
                break;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (keystates[p->scanCode] == 1) {
                    event.key.type = GE_KEYUP;
                    event.key.keysym = keyCode;
                    event_callback(&event);
                    keystates[p->scanCode] = 0;
                }
                break;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static HHOOK LLKeyboardHook = NULL;
static HHOOK LLMouseHook = NULL;

void llh_quit(void) {

    if (LLMouseHook != NULL) {
        UnhookWindowsHookEx(LLMouseHook);
    }
    if (LLKeyboardHook != NULL) {
        UnhookWindowsHookEx(LLKeyboardHook);
    }
}

int llh_init(const GPOLL_INTERFACE * poll_interface __attribute__((unused)), int (*callback)(GE_Event*)) {

    if (callback == NULL) {
        PRINT_ERROR_OTHER("callback is NULL")
        return -1;
    }

    event_callback = callback;

    LLKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (LLKeyboardHook == NULL) {
        PRINT_ERROR_GETLASTERROR("SetWindowsHookEx (WH_KEYBOARD_LL)")
        llh_quit();
        return -1;
    }
    LLMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
    if (LLMouseHook == NULL) {
        PRINT_ERROR_GETLASTERROR("SetWindowsHookEx (WH_MOUSE_LL)")
        llh_quit();
        return -1;
    }

    return 0;
}

const char * llh_mouse_name(int index) {

    return (index == 0) ? "low level mouse hook" : NULL;
}

const char * llh_keyboard_name(int index) {

    return (index == 0) ? "low level keyboard hook" : NULL;
}

int llh_poll() {

    MSG Msg;
    while (PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }

    return 0;
}

static int llh_get_src() {

    return GE_MKB_SOURCE_LOW_LEVEL_HOOKS;
}

static struct mkb_source llh_source = {
        .init = llh_init,
        .get_src = llh_get_src,
        .grab = NULL,
        .get_mouse_name = llh_mouse_name,
        .get_keyboard_name = llh_keyboard_name,
        .sync_process = llh_poll,
        .quit = llh_quit,
};

void llh_constructor() __attribute__((constructor));
void llh_constructor() {
    ev_register_mkb_source(&llh_source);
}
