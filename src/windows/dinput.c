/*
 Copyright (c) 2018 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "scancodes.h"
#include "../events.h"

#include <windows.h>
#include <dinput.h>

#include <gimxcommon/include/gerror.h>
#include <gimxlog/include/glog.h>

#define MAX_KEYS 256

GLOG_GET(GLOG_NAME)

static int (*event_callback)(GE_Event*) = NULL;

static unsigned char keystates[MAX_KEYS] = { };

static LPDIRECTINPUTDEVICE8 mouse = NULL;
static LPDIRECTINPUTDEVICE8 keyboard = NULL;
static LPDIRECTINPUT8 dinput = NULL;
static HWND di_hwnd = NULL;
static ATOM class_atom = 0;
static const char * class_name = DIRECTINPUT_CLASS_NAME;
static const char * win_name = DIRECTINPUT_WINDOW_NAME;

void dinput_quit(void) {

    if (mouse != NULL) {
        IDirectInputDevice8_Unacquire(mouse);
        IDirectInputDevice8_Release(mouse);
    }
    if (keyboard != NULL) {
        IDirectInputDevice8_Unacquire(keyboard);
        IDirectInputDevice8_Release(keyboard);
    }

    if (dinput != NULL) {
        IDirectInput_Release(dinput);
    }

    if (di_hwnd != NULL) {
        MSG Msg;
        DestroyWindow(di_hwnd);
        while (PeekMessage(&Msg, di_hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&Msg);
            DispatchMessage(&Msg);
        }
        di_hwnd = NULL;
    }

    if (class_atom != 0) {
        UnregisterClass(class_name, GetModuleHandle(NULL));
        class_atom = 0;
    }
}

static LRESULT CALLBACK RawWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {

    if (Msg == WM_DESTROY) {
        return 0;
    }

    return DefWindowProc(hWnd, Msg, wParam, lParam);
}

int dinput_init(const GPOLL_INTERFACE * poll_interface __attribute__((unused)), int (*callback)(GE_Event*)) {

    if (callback == NULL) {
        PRINT_ERROR_OTHER("callback is NULL")
        return -1;
    }

    event_callback = callback;

    HANDLE hInstance = GetModuleHandle(NULL);

    WNDCLASSEX wce = {
            .cbSize = sizeof(WNDCLASSEX),
            .lpfnWndProc = RawWndProc,
            .lpszClassName = class_name,
            .hInstance = hInstance,
    };
    class_atom = RegisterClassEx(&wce);
    if (class_atom == 0) {
        return -1;
    }

    POINT cursor_pos;
    GetCursorPos(&cursor_pos);

    di_hwnd = CreateWindow(class_name, win_name, WS_POPUP | WS_VISIBLE | WS_SYSMENU, cursor_pos.x, cursor_pos.y, 1, 1,
            NULL, NULL, hInstance, NULL);

    if (di_hwnd == NULL) {
        PRINT_ERROR_GETLASTERROR("CreateWindow failed")
        return -1;
    }

    ShowWindow(di_hwnd, SW_MINIMIZE);
    ShowWindow(di_hwnd, SW_RESTORE);

    HRESULT hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8, (VOID**) &dinput,
            NULL);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("DirectInput8Create failed")
        return -1;
    }

    hr = IDirectInput8_CreateDevice(dinput, &GUID_SysMouse, &mouse, NULL);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("IDirectInput8_CreateDevice failed")
        return -1;
    }

    hr = IDirectInputDevice8_SetDataFormat(mouse, &c_dfDIMouse2);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("IDirectInputDevice8_SetDataFormat failed")
        return hr;
    }

    hr = IDirectInputDevice8_SetCooperativeLevel(mouse, di_hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
//    hr = IDirectInputDevice8_SetCooperativeLevel(mouse, di_hwnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("IDirectInputDevice8_SetCooperativeLevel failed")
        return -1;
    }

    hr = IDirectInputDevice8_Acquire(mouse);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("IDirectInputDevice8_Acquire failed")
        //return -1;
    }

    hr = IDirectInput8_CreateDevice(dinput, &GUID_SysKeyboard, &keyboard, NULL);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("IDirectInput8_CreateDevice failed")
        return -1;
    }

    hr = IDirectInputDevice8_SetDataFormat(keyboard, &c_dfDIKeyboard);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("IDirectInputDevice8_SetDataFormat failed")
        return hr;
    }

    hr = IDirectInputDevice8_SetCooperativeLevel(keyboard, di_hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
//    hr = IDirectInputDevice8_SetCooperativeLevel(keyboard, di_hwnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("IDirectInputDevice8_SetCooperativeLevel failed")
        return -1;
    }

    hr = IDirectInputDevice8_Acquire(keyboard);
    if (FAILED(hr)) {
        PRINT_ERROR_OTHER("IDirectInputDevice8_Acquire failed")
        //return -1;
    }

    return 0;
}

const char * dinput_mouse_name(int index) {

    return (index == 0) ? "dinput mouse" : NULL;
}

const char * dinput_keyboard_name(int index) {

    return (index == 0) ? "dinput keyboard" : NULL;
}

int dinput_poll() {

    MSG Msg;
    while (PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }

    DIMOUSESTATE2 dims2;
    ZeroMemory(&dims2, sizeof(dims2));

    HRESULT hr = IDirectInputDevice8_GetDeviceState(mouse, sizeof(DIMOUSESTATE2), &dims2);
    if (FAILED(hr)) {
        hr = IDirectInputDevice8_Acquire(mouse);
        while (hr == DIERR_INPUTLOST) {
            hr = IDirectInputDevice8_Acquire(mouse);
        }

        if (hr == DIERR_INPUTLOST) {
            PRINT_ERROR_OTHER("IDirectInputDevice8_Acquire failed")
            return -1;
        }
    }

    GE_Event event = { };
    event.type = GE_MOUSEMOTION;
    event.motion.xrel = dims2.lX;
    event.motion.yrel = 0;
    event_callback(&event);
    event.motion.xrel = 0;
    event.motion.yrel = dims2.lY;
    event_callback(&event);

    return 0;
}

static int dinput_get_src() {

    return GE_MKB_SOURCE_DINPUT;
}

static struct mkb_source dinput_source = {
        .init = dinput_init,
        .get_src = dinput_get_src,
        .grab = NULL,
        .get_mouse_name = dinput_mouse_name,
        .get_keyboard_name = dinput_keyboard_name,
        .sync_process = dinput_poll,
        .quit = dinput_quit,
};

void dinput_constructor() __attribute__((constructor));
void dinput_constructor() {
    ev_register_mkb_source(&dinput_source);
}
