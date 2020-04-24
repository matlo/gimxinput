/*
 Copyright (c) 2018 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef GINPUT_H_
#define GINPUT_H_

#include <stdint.h>
#include <gimxpoll/include/gpoll.h>

#define GE_MAX_DEVICES 256

#define GE_MOUSE_BUTTONS_MAX 12

#ifdef WIN32
#include "ginput_windows.h"
typedef void* HANDLE;
#else
#include "ginput_linux.h"
#endif

#define GE_MKB_SOURCE_NONE 0
#define GE_MKB_SOURCE_PHYSICAL 1
#define GE_MKB_SOURCE_WINDOW_SYSTEM 2

#define GE_GRAB_OFF 0
#define GE_GRAB_ON 1

#ifndef KEY_MICMUTE
#define KEY_MICMUTE 248
#endif

#define GE_KEY_ESC  KEY_ESC
#define GE_KEY_1  KEY_1
#define GE_KEY_2  KEY_2
#define GE_KEY_3  KEY_3
#define GE_KEY_4  KEY_4
#define GE_KEY_5  KEY_5
#define GE_KEY_6  KEY_6
#define GE_KEY_7  KEY_7
#define GE_KEY_8  KEY_8
#define GE_KEY_9  KEY_9
#define GE_KEY_0  KEY_0
#define GE_KEY_MINUS  KEY_MINUS
#define GE_KEY_EQUAL  KEY_EQUAL
#define GE_KEY_BACKSPACE  KEY_BACKSPACE
#define GE_KEY_TAB  KEY_TAB
#define GE_KEY_Q  KEY_Q
#define GE_KEY_W  KEY_W
#define GE_KEY_E  KEY_E
#define GE_KEY_R  KEY_R
#define GE_KEY_T  KEY_T
#define GE_KEY_Y  KEY_Y
#define GE_KEY_U  KEY_U
#define GE_KEY_I  KEY_I
#define GE_KEY_O  KEY_O
#define GE_KEY_P  KEY_P
#define GE_KEY_LEFTBRACE  KEY_LEFTBRACE
#define GE_KEY_RIGHTBRACE   KEY_RIGHTBRACE
#define GE_KEY_ENTER  KEY_ENTER
#define GE_KEY_LEFTCTRL   KEY_LEFTCTRL
#define GE_KEY_A  KEY_A
#define GE_KEY_S  KEY_S
#define GE_KEY_D  KEY_D
#define GE_KEY_F  KEY_F
#define GE_KEY_G  KEY_G
#define GE_KEY_H  KEY_H
#define GE_KEY_J  KEY_J
#define GE_KEY_K  KEY_K
#define GE_KEY_L  KEY_L
#define GE_KEY_SEMICOLON  KEY_SEMICOLON
#define GE_KEY_APOSTROPHE   KEY_APOSTROPHE
#define GE_KEY_GRAVE  KEY_GRAVE
#define GE_KEY_LEFTSHIFT  KEY_LEFTSHIFT
#define GE_KEY_BACKSLASH  KEY_BACKSLASH
#define GE_KEY_Z  KEY_Z
#define GE_KEY_X  KEY_X
#define GE_KEY_C  KEY_C
#define GE_KEY_V  KEY_V
#define GE_KEY_B  KEY_B
#define GE_KEY_N  KEY_N
#define GE_KEY_M  KEY_M
#define GE_KEY_COMMA  KEY_COMMA
#define GE_KEY_DOT  KEY_DOT
#define GE_KEY_SLASH  KEY_SLASH
#define GE_KEY_RIGHTSHIFT   KEY_RIGHTSHIFT
#define GE_KEY_KPASTERISK   KEY_KPASTERISK
#define GE_KEY_LEFTALT  KEY_LEFTALT
#define GE_KEY_SPACE  KEY_SPACE
#define GE_KEY_CAPSLOCK   KEY_CAPSLOCK
#define GE_KEY_F1   KEY_F1
#define GE_KEY_F2   KEY_F2
#define GE_KEY_F3   KEY_F3
#define GE_KEY_F4   KEY_F4
#define GE_KEY_F5   KEY_F5
#define GE_KEY_F6   KEY_F6
#define GE_KEY_F7   KEY_F7
#define GE_KEY_F8   KEY_F8
#define GE_KEY_F9   KEY_F9
#define GE_KEY_F10  KEY_F10
#define GE_KEY_NUMLOCK  KEY_NUMLOCK
#define GE_KEY_SCROLLLOCK   KEY_SCROLLLOCK
#define GE_KEY_KP7  KEY_KP7
#define GE_KEY_KP8  KEY_KP8
#define GE_KEY_KP9  KEY_KP9
#define GE_KEY_KPMINUS  KEY_KPMINUS
#define GE_KEY_KP4  KEY_KP4
#define GE_KEY_KP5  KEY_KP5
#define GE_KEY_KP6  KEY_KP6
#define GE_KEY_KPPLUS   KEY_KPPLUS
#define GE_KEY_KP1  KEY_KP1
#define GE_KEY_KP2  KEY_KP2
#define GE_KEY_KP3  KEY_KP3
#define GE_KEY_KP0  KEY_KP0
#define GE_KEY_KPDOT  KEY_KPDOT

#define GE_KEY_ZENKAKUHANKAKU   KEY_ZENKAKUHANKAKU
#define GE_KEY_102ND  KEY_102ND
#define GE_KEY_F11  KEY_F11
#define GE_KEY_F12  KEY_F12
#define GE_KEY_RO   KEY_RO
#define GE_KEY_KATAKANA   KEY_KATAKANA
#define GE_KEY_HIRAGANA   KEY_HIRAGANA
#define GE_KEY_HENKAN   KEY_HENKAN
#define GE_KEY_KATAKANAHIRAGANA   KEY_KATAKANAHIRAGANA
#define GE_KEY_MUHENKAN   KEY_MUHENKAN
#define GE_KEY_KPJPCOMMA  KEY_KPJPCOMMA
#define GE_KEY_KPENTER  KEY_KPENTER
#define GE_KEY_RIGHTCTRL  KEY_RIGHTCTRL
#define GE_KEY_KPSLASH  KEY_KPSLASH
#define GE_KEY_SYSRQ  KEY_SYSRQ
#define GE_KEY_RIGHTALT   KEY_RIGHTALT
#define GE_KEY_LINEFEED   KEY_LINEFEED
#define GE_KEY_HOME   KEY_HOME
#define GE_KEY_UP   KEY_UP
#define GE_KEY_PAGEUP   KEY_PAGEUP
#define GE_KEY_LEFT   KEY_LEFT
#define GE_KEY_RIGHT  KEY_RIGHT
#define GE_KEY_END  KEY_END
#define GE_KEY_DOWN   KEY_DOWN
#define GE_KEY_PAGEDOWN   KEY_PAGEDOWN
#define GE_KEY_INSERT   KEY_INSERT
#define GE_KEY_DELETE   KEY_DELETE
#define GE_KEY_MACRO  KEY_MACRO
#define GE_KEY_MUTE   KEY_MUTE
#define GE_KEY_VOLUMEDOWN   KEY_VOLUMEDOWN
#define GE_KEY_VOLUMEUP   KEY_VOLUMEUP
#define GE_KEY_POWER  KEY_POWER
#define GE_KEY_KPEQUAL  KEY_KPEQUAL
#define GE_KEY_KPPLUSMINUS  KEY_KPPLUSMINUS
#define GE_KEY_PAUSE  KEY_PAUSE
#define GE_KEY_SCALE  KEY_SCALE

#define GE_KEY_KPCOMMA  KEY_KPCOMMA
#define GE_KEY_HANGEUL  KEY_HANGEUL
#define GE_KEY_HANGUEL  KEY_HANGEUL
#define GE_KEY_HANJA  KEY_HANJA
#define GE_KEY_YEN  KEY_YEN
#define GE_KEY_LEFTMETA   KEY_LEFTMETA
#define GE_KEY_RIGHTMETA  KEY_RIGHTMETA
#define GE_KEY_COMPOSE  KEY_COMPOSE

#define GE_KEY_STOP   KEY_STOP
#define GE_KEY_AGAIN  KEY_AGAIN
#define GE_KEY_PROPS  KEY_PROPS
#define GE_KEY_UNDO   KEY_UNDO
#define GE_KEY_FRONT  KEY_FRONT
#define GE_KEY_COPY   KEY_COPY
#define GE_KEY_OPEN   KEY_OPEN
#define GE_KEY_PASTE  KEY_PASTE
#define GE_KEY_FIND   KEY_FIND
#define GE_KEY_CUT  KEY_CUT
#define GE_KEY_HELP   KEY_HELP
#define GE_KEY_MENU   KEY_MENU
#define GE_KEY_CALC   KEY_CALC
#define GE_KEY_SETUP  KEY_SETUP
#define GE_KEY_SLEEP  KEY_SLEEP
#define GE_KEY_WAKEUP   KEY_WAKEUP
#define GE_KEY_FILE   KEY_FILE
#define GE_KEY_SENDFILE   KEY_SENDFILE
#define GE_KEY_DELETEFILE   KEY_DELETEFILE
#define GE_KEY_XFER   KEY_XFER
#define GE_KEY_PROG1  KEY_PROG1
#define GE_KEY_PROG2  KEY_PROG2
#define GE_KEY_WWW  KEY_WWW
#define GE_KEY_MSDOS  KEY_MSDOS
#define GE_KEY_COFFEE   KEY_COFFEE
#define GE_KEY_SCREENLOCK   KEY_COFFEE
#define GE_KEY_DIRECTION  KEY_DIRECTION
#define GE_KEY_CYCLEWINDOWS   KEY_CYCLEWINDOWS
#define GE_KEY_MAIL   KEY_MAIL
#define GE_KEY_BOOKMARKS  KEY_BOOKMARKS
#define GE_KEY_COMPUTER   KEY_COMPUTER
#define GE_KEY_BACK   KEY_BACK
#define GE_KEY_FORWARD  KEY_FORWARD
#define GE_KEY_CLOSECD  KEY_CLOSECD
#define GE_KEY_EJECTCD  KEY_EJECTCD
#define GE_KEY_EJECTCLOSECD   KEY_EJECTCLOSECD
#define GE_KEY_NEXTSONG   KEY_NEXTSONG
#define GE_KEY_PLAYPAUSE  KEY_PLAYPAUSE
#define GE_KEY_PREVIOUSSONG   KEY_PREVIOUSSONG
#define GE_KEY_STOPCD   KEY_STOPCD
#define GE_KEY_RECORD   KEY_RECORD
#define GE_KEY_REWIND   KEY_REWIND
#define GE_KEY_PHONE  KEY_PHONE
#define GE_KEY_ISO  KEY_ISO
#define GE_KEY_CONFIG   KEY_CONFIG
#define GE_KEY_HOMEPAGE   KEY_HOMEPAGE
#define GE_KEY_REFRESH  KEY_REFRESH
#define GE_KEY_EXIT   KEY_EXIT
#define GE_KEY_MOVE   KEY_MOVE
#define GE_KEY_EDIT   KEY_EDIT
#define GE_KEY_SCROLLUP   KEY_SCROLLUP
#define GE_KEY_SCROLLDOWN   KEY_SCROLLDOWN
#define GE_KEY_KPLEFTPAREN  KEY_KPLEFTPAREN
#define GE_KEY_KPRIGHTPAREN   KEY_KPRIGHTPAREN
#define GE_KEY_NEW  KEY_NEW
#define GE_KEY_REDO   KEY_REDO

#define GE_KEY_F13  KEY_F13
#define GE_KEY_F14  KEY_F14
#define GE_KEY_F15  KEY_F15
#define GE_KEY_F16  KEY_F16
#define GE_KEY_F17  KEY_F17
#define GE_KEY_F18  KEY_F18
#define GE_KEY_F19  KEY_F19
#define GE_KEY_F20  KEY_F20
#define GE_KEY_F21  KEY_F21
#define GE_KEY_F22  KEY_F22
#define GE_KEY_F23  KEY_F23
#define GE_KEY_F24  KEY_F24

#define GE_KEY_PLAYCD   KEY_PLAYCD
#define GE_KEY_PAUSECD  KEY_PAUSECD
#define GE_KEY_PROG3  KEY_PROG3
#define GE_KEY_PROG4  KEY_PROG4
#define GE_KEY_DASHBOARD  KEY_DASHBOARD
#define GE_KEY_SUSPEND  KEY_SUSPEND
#define GE_KEY_CLOSE  KEY_CLOSE
#define GE_KEY_PLAY   KEY_PLAY
#define GE_KEY_FASTFORWARD  KEY_FASTFORWARD
#define GE_KEY_BASSBOOST  KEY_BASSBOOST
#define GE_KEY_PRINT  KEY_PRINT
#define GE_KEY_HP   KEY_HP
#define GE_KEY_CAMERA   KEY_CAMERA
#define GE_KEY_SOUND  KEY_SOUND
#define GE_KEY_QUESTION   KEY_QUESTION
#define GE_KEY_EMAIL  KEY_EMAIL
#define GE_KEY_CHAT   KEY_CHAT
#define GE_KEY_SEARCH   KEY_SEARCH
#define GE_KEY_CONNECT  KEY_CONNECT
#define GE_KEY_FINANCE  KEY_FINANCE
#define GE_KEY_SPORT  KEY_SPORT
#define GE_KEY_SHOP   KEY_SHOP
#define GE_KEY_ALTERASE   KEY_ALTERASE
#define GE_KEY_CANCEL   KEY_CANCEL
#define GE_KEY_BRIGHTNESSDOWN   KEY_BRIGHTNESSDOWN
#define GE_KEY_BRIGHTNESSUP   KEY_BRIGHTNESSUP
#define GE_KEY_MEDIA  KEY_MEDIA

#define GE_KEY_SWITCHVIDEOMODE  KEY_SWITCHVIDEOMODE
#define GE_KEY_KBDILLUMTOGGLE   KEY_KBDILLUMTOGGLE
#define GE_KEY_KBDILLUMDOWN   KEY_KBDILLUMDOWN
#define GE_KEY_KBDILLUMUP   KEY_KBDILLUMUP

#define GE_KEY_SEND   KEY_SEND
#define GE_KEY_REPLY  KEY_REPLY
#define GE_KEY_FORWARDMAIL  KEY_FORWARDMAIL
#define GE_KEY_SAVE   KEY_SAVE
#define GE_KEY_DOCUMENTS  KEY_DOCUMENTS

#define GE_KEY_BATTERY  KEY_BATTERY

#define GE_KEY_BLUETOOTH  KEY_BLUETOOTH
#define GE_KEY_WLAN   KEY_WLAN
#define GE_KEY_UWB  KEY_UWB

#define GE_KEY_UNKNOWN  KEY_UNKNOWN

#define GE_KEY_VIDEO_NEXT   KEY_VIDEO_NEXT
#define GE_KEY_VIDEO_PREV   KEY_VIDEO_PREV
#define GE_KEY_BRIGHTNESS_CYCLE   KEY_BRIGHTNESS_CYCLE
#define GE_KEY_BRIGHTNESS_ZERO  KEY_BRIGHTNESS_ZERO
#define GE_KEY_DISPLAY_OFF  KEY_DISPLAY_OFF

#define GE_KEY_WIMAX  KEY_WIMAX
#define GE_KEY_RFKILL   KEY_RFKILL

#define GE_KEY_MICMUTE  KEY_MICMUTE

typedef enum {
       GE_NOEVENT = 0,     /**< Unused (do not remove) */
       GE_KEYDOWN,     /**< Keys pressed */
       GE_KEYUP,     /**< Keys released */
       GE_MOUSEMOTION,     /**< Mouse moved */
       GE_MOUSEBUTTONDOWN,   /**< Mouse button pressed */
       GE_MOUSEBUTTONUP,   /**< Mouse button released */
       GE_JOYAXISMOTION,   /**< Joystick axis motion */
       GE_JOYHATMOTION,    /**< Joystick hat position change */
       GE_JOYBUTTONDOWN,   /**< Joystick button pressed */
       GE_JOYBUTTONUP,     /**< Joystick button released */
       GE_JOYRUMBLE,     /**< Joystick rumble */
       GE_JOYCONSTANTFORCE,     /**< Joystick constant force */
       GE_JOYSPRINGFORCE,     /**< Joystick spring force */
       GE_JOYDAMPERFORCE,     /**< Joystick damper force */
       GE_JOYSINEFORCE,     /**< Joystick sine force */
       GE_QUIT,
} GE_EventType;

typedef struct GE_KeyboardEvent {
  uint8_t type; /**< GE_KEYDOWN or GE_KEYUP */
  uint8_t which;  /**< The keyboard device index */
  uint16_t keysym;
} GE_KeyboardEvent;

typedef struct GE_MouseMotionEvent {
  uint8_t type; /**< GE_MOUSEMOTION */
  uint8_t which;  /**< The mouse device index */
  int16_t xrel;  /**< The relative motion in the X direction */
  int16_t yrel;  /**< The relative motion in the Y direction */
} GE_MouseMotionEvent;

typedef struct GE_MouseButtonEvent {
  uint8_t type; /**< GE_MOUSEBUTTONDOWN or GE_MOUSEBUTTONUP */
  uint8_t which;  /**< The mouse device index */
  uint8_t button; /**< The mouse button index */
} GE_MouseButtonEvent;

typedef struct GE_JoyAxisEvent {
  uint8_t type; /**< GE_JOYAXISMOTION */
  uint8_t which;  /**< The joystick device index */
  uint8_t axis; /**< The joystick axis index */
  int16_t value; /**< The axis value (range: -32768 to 32767) */
} GE_JoyAxisEvent;

typedef struct GE_JoyHatEvent {
  uint8_t type; /**< GE_JOYHATMOTION */
  uint8_t which;  /**< The joystick device index */
  uint8_t hat;  /**< The joystick hat index */
  uint8_t value;  /**< The hat position value:
       *   GE_HAT_LEFTUP   GE_HAT_UP       GE_HAT_RIGHTUP
       *   GE_HAT_LEFT     GE_HAT_CENTERED GE_HAT_RIGHT
       *   GE_HAT_LEFTDOWN GE_HAT_DOWN     GE_HAT_RIGHTDOWN
       *  Note that zero means the POV is centered.
       */
} GE_JoyHatEvent;

typedef struct GE_JoyButtonEvent {
  uint8_t type; /**< GE_JOYBUTTONDOWN or GE_JOYBUTTONUP */
  uint8_t which;  /**< The joystick device index */
  uint8_t button; /**< The joystick button index */
  uint8_t state;  /**< GE_PRESSED or GE_RELEASED */
} GE_JoyButtonEvent;

typedef struct GE_JoyRumbleEvent {
  uint8_t type; /**< GE_JOYRUMBLE */
  uint8_t which;  /**< The joystick device index */
  uint16_t weak; /**< Weak motor */
  uint16_t strong;  /**< Strong motor */
} GE_JoyRumbleEvent;

typedef struct GE_JoyConstantForceEvent {
  uint8_t type;     /**< GE_JOYCONSTANTFORCE */
  uint8_t which;  /**< The joystick device index */
  int16_t level;
} GE_JoyConstantForceEvent;

typedef struct GE_JoyConditionForceEvent {
  uint8_t type;     /**< GE_JOYSPRINGFORCE or GE_JOYDAMPERFORCE */
  uint8_t which;  /**< The joystick device index */
  struct {
    uint16_t left;
    uint16_t right;
  } saturation;
  struct {
    int16_t left;
    int16_t right;
  } coefficient;
  int16_t center;
  uint16_t deadband;
} GE_JoyConditionForceEvent;

typedef struct GE_JoyPeriodicForceEvent {
  uint8_t type;     /**< GE_JOYSINEFORCE */
  uint8_t which;  /**< The joystick device index */
  struct {
    int32_t direction; /**< polar coordinates (0 = North, 9000 = East, 18000 = South, 27000 = West) - not available on GNU/Linux */
    uint16_t period;
    int16_t magnitude;
    int16_t offset;
  } sine;
} GE_JoyPeriodicForceEvent;

typedef union GE_Event {
  struct
  {
    uint8_t type;
    uint8_t which;
  };
  GE_KeyboardEvent key;
  GE_MouseMotionEvent motion;
  GE_MouseButtonEvent button;
  GE_JoyAxisEvent jaxis;
  GE_JoyHatEvent jhat;
  GE_JoyButtonEvent jbutton;
  GE_JoyRumbleEvent jrumble;
  GE_JoyConstantForceEvent jconstant;
  GE_JoyConditionForceEvent jcondition;
  GE_JoyPeriodicForceEvent jperiodic;
} GE_Event;

typedef enum
{
  GE_HAPTIC_NONE     = 0x00,
  GE_HAPTIC_RUMBLE   = 0x01,
  GE_HAPTIC_CONSTANT = 0x02,
  GE_HAPTIC_SPRING   = 0x04,
  GE_HAPTIC_DAMPER   = 0x08,
  GE_HAPTIC_SINE     = 0x10,
} GE_HapticType;

typedef enum
{
  GE_MK_MODE_MULTIPLE_INPUTS,
  GE_MK_MODE_SINGLE_INPUT
} GE_MK_Mode;

#define EVENT_BUFFER_SIZE 256

#define AXIS_X 0
#define AXIS_Y 1

#define MOUSE_AXIS_X "x"
#define MOUSE_AXIS_Y "y"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * \bried Initializes the library.
 *
 * \param poll_interface  the poll interface (register and remove functions)
 * \param mkb_src         GE_MKB_SOURCE_PHYSICAL: use evdev under Linux and raw inputs under Windows.
 *                        GE_MKB_SOURCE_WINDOW_SYSTEM: use X inputs under Linux and the SDL library under Windows.
 * \param callback        the callback to process input events (cannot be NULL)
 *
 * \return 1 if successful
 *         0 in case of error
 */
int ginput_init(const GPOLL_INTERFACE * poll_interface, unsigned char mkb_src, int(*callback)(GE_Event*));

/*
 * \brief Grab/Release the mouse cursor (Windows) or grab/release all keyboard and mouse event devices (Linux).
 *
 * \return 1 if mouse is grabbed, 0 otherwise
 */
int ginput_grab_toggle();

/*
 * \brief Grab the mouse.
 */
void ginput_grab();

/*
 * \brief Release unused stuff. It currently only releases unused joysticks.
 */
void ginput_release_unused();

/*
 * \brief Quit the library (free allocated data, release devices...).
 */
void ginput_quit();

/*
 * \brief Free the mouse and keyboard names.
 */
void ginput_free_mk_names();

/*
 * \brief Get the mouse name for a given index.
 *
 * \param id  the mouse index (in the [0..GE_MAX_DEVICES[ range).
 *
 * \return the mouse name if present, NULL otherwise.
 */
const char * ginput_mouse_name(int id);

/*
 * \brief Get the keyboard name for a given index.
 *
 * \param id  the keyboard index (in the [0..GE_MAX_DEVICES[ range)
 *
 * \return the keyboard name if present, NULL otherwise.
 */
const char * ginput_keyboard_name(int id);

/*
 * \brief Get the mouse virtual id for a given index.
 *
 * \param id  the mouse index (in the [0..GE_MAX_DEVICES[ range)
 *
 * \return the mouse virtual id if present, NULL otherwise.
 */
int ginput_mouse_virtual_id(int id);

/*
 * \brief Get the keyboard virtual id for a given index.
 *
 * \param id  the keyboard index (in the [0..GE_MAX_DEVICES[ range)
 *
 * \return the keyboard virtual id if present, NULL otherwise.
 */
int ginput_keyboard_virtual_id(int id);

/*
 * \brief Returns the device id corresponding to a given event.
 *
 * \param e  the event
 *
 * \return the device id (0 if the event is from a mouse or a keyboard and the mk mode is GE_MK_MODE_SINGLE_INPUT).
 */
int ginput_get_device_id(GE_Event* e);

/*
 * \brief Get the joystick name for a given index.
 *
 * \param id  the joystick index (in the [0..GE_MAX_DEVICES[ range)
 *
 * \return the joystick name if present, NULL otherwise.
 */
const char * ginput_joystick_name(int id);

/*
 * \brief Get the joystick virtual id for a given index.
 *
 * \param id  the joystick index (in the [0..GE_MAX_DEVICES[ range)
 *
 * \return the joystick virtual id if present, NULL otherwise.
 */
int ginput_joystick_virtual_id(int id);

/*
 * \brief Set a joystick to the "used" state, so that a call to ginput_release_unused will keep it open.
 *
 * \param id  the joystick index (in the [0..GE_MAX_DEVICES[ range)
 */
void ginput_set_joystick_used(int id);

/*
 * \brief Register a joystick to be emulated in software.
 *
 * \remark This function has to be called before calling ginput_init.
 *
 * \param name      the name of the joystick to register.
 * \param haptic    the haptic capabilities (bitfield of GE_HapticType values).
 * \param haptic_cb the haptic callback.
 *
 * \return the id of the joystick, that can be used to forge a GE_Event,
 *         or -1 if the library was already initialized
 */
int ginput_register_joystick(const char* name, unsigned int haptic, int (*haptic_cb)(const GE_Event * event));

/*
 * \brief Get the button name for a given button id.
 *
 * \param button  the button id
 *
 * \return the button name
 */
const char * ginput_mouse_button_name(int button);

/*
 * \brief Get the button id for a given button name.
 *
 * \param name  the button name
 *
 * \return the button id
 */
int ginput_mouse_button_id(const char* name);

/*
 * \brief Get the key name for a given key id.
 *
 * \param key  the key id
 *
 * \return the key name
 */
const char * ginput_key_name(uint16_t key);

/*
 * \brief Get the key id for a given key name.
 *
 * \param name  the key name
 *
 * \return the key id
 */
uint16_t ginput_key_id(const char* name);

/*
 * \brief Get the mk mode.
 *
 * \return value GE_MK_MODE_MULTIPLE_INPUTS multiple mice and  multiple keyboards (default value),
 *               GE_MK_MODE_SINGLE_INPUT    single mouse and single keyboard
 */
GE_MK_Mode ginput_get_mk_mode();

/*
 * \brief Set the mk mode.
 *
 * \param value GE_MK_MODE_MULTIPLE_INPUTS multiple mice and  multiple keyboards (default value),
 *              GE_MK_MODE_SINGLE_INPUT    single mouse and single keyboard
 */
void ginput_set_mk_mode(GE_MK_Mode value);

/*
 * \brief Return the haptic capabilities of a joystick.
 *
 * \param id  the joystick index (in the [0..GE_MAX_DEVICES[ range)
 *
 * \return the haptic capabilities (bitfield of GE_HapticType values) or -1 in case of error.
 */
int ginput_joystick_get_haptic(int id);

/*
 * \brief Set a joystick haptic effect.
 *
 * \param event the haptic event, with haptic.which the joystick index (in the [0..GE_MAX_DEVICES[ range).
 *
 * \return -1 in case of error, 0 otherwise
 */
int ginput_joystick_set_haptic(const GE_Event * event);

/*
 * \brief Get all events from the event queue.
 *
 * \param events  the buffer to store the events
 * \param numevents  the max number of events to retrieve
 *
 * \return the number of retrieved events.
 */
int ginput_queue_pop(GE_Event *events, int numevents);

/*
 * \brief Push an event into the event queue.
 *
 * \param event  the event
 *
 * \return 0 in case of success, -1 in case of error.
 */
int ginput_queue_push(GE_Event *event);

#ifdef WIN32
/*
 * \brief Get the USB VID and PID of a joystick.
 *        This function is Windows-specific.
 *
 * \param id       the joystick index (in the [0..GE_MAX_DEVICES[ range)
 * \param vendor   where to store the USB VID
 * \param product  where to store the USB PID
 *
 * \return 0 in case of success, -1 in case of error.
 */
int ginput_joystick_get_usb_ids(int id, unsigned short * vendor, unsigned short * product);
#else
/*
 * \brief Get the HID device of a joystick. The library uses HID for a few devices.
 *        This function is Linux-specific.
 *
 * \param id       the joystick index (in the [0..GE_MAX_DEVICES[ range)
 * \param vendor   where to store the USB VID
 * \param product  where to store the USB PID
 *
 * \return the HID device, or NULL if library does not use HID for this joystick or in case of error.
 */
void * ginput_joystick_get_hid(int id);

/*
 * \brief Set the write and close callbacks for a HID device.
 *        This function is Linux-specific.
 *
 * \param dev           the HID device (returned by ginput_joystick_get_hid)
 * \param user          a pointer to provide context to the callbacks
 * \param hid_write_cb  the write callback
 * \param hid_close_cb  the close callback
 *
 * \return 0 in case of success, -1 in case of error.
 */
int ginput_joystick_set_hid_callbacks(void * dev, void * user, int (* hid_write_cb)(void * user, int status), int (* hid_close_cb)(void * user));
#endif

/*
 * \brief Process all events from non-asynchronous sources.
 *        Poll all hidinput devices.
 */
void ginput_periodic_task();

#ifdef __cplusplus
}
#endif

#endif /* GINPUT_H_ */
