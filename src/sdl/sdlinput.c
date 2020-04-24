/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <math.h>
#include "keycodes.h"

#include <SDL.h>

#include <ginput.h>
#include <gimxcommon/include/gerror.h>
#include <gimxcommon/include/glist.h>
#include <gimxlog/include/glog.h>
#include "../events.h"

#define PRINT_ERROR_SDL(msg) \
    do { \
        if (GLOG_LEVEL(GLOG_NAME,ERROR)) { \
            fprintf(stderr, "%s:%d %s: %s failed with error: %s\n", __FILE__, __LINE__, __func__, msg, SDL_GetError()); \
        } \
    } while (0)

#define PRINT_DEBUG_SDL(msg) \
    do { \
        if (GLOG_LEVEL(GLOG_NAME,DEBUG)) { \
            fprintf(stderr, "%s:%d %s: %s failed with error: %s\n", __FILE__, __LINE__, __func__, msg, SDL_GetError()); \
        } \
    } while (0)

// mouse capture is broken with a 1x1 window and "fix scaling for apps" enabled
#define SCREEN_WIDTH  2
#define SCREEN_HEIGHT 2

GLOG_GET(GLOG_NAME)

// The SDL has reference counters for each subsystem (since version 2.00).

// SDL_INIT_GAMECONTROLLER adds SDL_INIT_JOYSTICK, which in turn adds SDL_INIT_EVENT
#define JOYSTICK_FLAGS (SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC)

// SDL_INIT_VIDEO adds SDL_INIT_EVENT
// Grabbing the mouse pointer requires a window.
#define MKB_FLAGS (SDL_INIT_VIDEO)

static struct
{
  unsigned int sdltype;
  GE_HapticType type;
  const char * name;
} effect_types[] =
{
  { SDL_HAPTIC_LEFTRIGHT, GE_HAPTIC_RUMBLE,   "rumble"   },
  { SDL_HAPTIC_CONSTANT,  GE_HAPTIC_CONSTANT, "constant" },
  { SDL_HAPTIC_SPRING,    GE_HAPTIC_SPRING,   "spring"   },
  { SDL_HAPTIC_DAMPER,    GE_HAPTIC_DAMPER,   "damper"   },
  { SDL_HAPTIC_SINE,      GE_HAPTIC_SINE,     "sine"     }
};

static int js_init = 0;
static int mkb_init = 0;

static SDL_Window* window = NULL;

static int sdlInstanceIdToIndex[GE_MAX_DEVICES] = {};

static int js_max_index = 0;
// Keep tracking of the number of registered joysticks (externally handled) and the
// number of opened joysticks, so as to be able to close the joystick subsystem
// and to avoid pumping the SDL library events when no joystick is used.
static int joysticks_registered = 0;
static int joysticks_opened = 0;

struct joystick_device {
    int index;
    char* name; // registered joysticks (externally handled)
    SDL_Joystick* joystick;
    SDL_GameController* controller;
    struct {
        SDL_Haptic* haptic;
        unsigned int effects;
        GE_HapticType emulate_rumble;
        int ids[sizeof(effect_types) / sizeof(*effect_types)];
        int (*haptic_cb)(const GE_Event * event);
        int hasSimpleRumble;
    } force_feedback;
    struct {
        int joystickHatButtonBaseIndex; // the base index of the generated hat buttons equals the number of physical buttons
        int joystickNbHat; // the number of hats
        unsigned char* joystickHat; // the current hat values
    } hat_info; // allows to convert hat axes to buttons
    struct {
        unsigned short vendor;
        unsigned short product;
    } usb_ids;
    GLIST_LINK(struct joystick_device);
};

static struct joystick_device * indexToJoystick[GE_MAX_DEVICES] = { };

#define CHECK_JS_DEVICE(INDEX, RETVALUE) \
    if(INDEX < 0 || INDEX >= GE_MAX_DEVICES || indexToJoystick[INDEX] == NULL) \
    { \
      return RETVALUE; \
    }

static int js_close_internal(void * user);

static GLIST_INST(struct joystick_device, sdl_devices);

static struct {
    short x;
    short y;
} mouse[GE_MAX_DEVICES] = { };

static int (*event_js_callback)(GE_Event*) = NULL;
static int (*event_mkb_callback)(GE_Event*) = NULL;

static int get_effect_id(struct joystick_device * device, GE_HapticType type) {

    int i = -1;
    switch (type) {
    case GE_HAPTIC_RUMBLE:
        i = 0;
        break;
    case GE_HAPTIC_CONSTANT:
        i = 1;
        break;
    case GE_HAPTIC_SPRING:
        i = 2;
        break;
    case GE_HAPTIC_DAMPER:
        i = 3;
        break;
    case GE_HAPTIC_SINE:
        i = 4;
        break;
    case GE_HAPTIC_NONE:
        break;
    }
    if (i < 0) {
        return -1;
    }
    return device->force_feedback.ids[i];
}

static void open_haptic(struct joystick_device * device, SDL_Joystick* joystick) {

    device->force_feedback.effects = GE_HAPTIC_NONE;

    SDL_Haptic* haptic = SDL_HapticOpenFromJoystick(joystick);
    if (haptic) {
        unsigned int features = SDL_HapticQuery(haptic);
        unsigned int i;
        for (i = 0; i < sizeof(effect_types) / sizeof(*effect_types); ++i) {
            if (features & effect_types[i].sdltype) {
                SDL_HapticEffect effect = { .type = effect_types[i].sdltype, };
                switch (effect_types[i].sdltype) {
                case SDL_HAPTIC_LEFTRIGHT:
                    effect.leftright.length = SDL_HAPTIC_INFINITY;
                    break;
                case SDL_HAPTIC_CONSTANT:
                    effect.constant.length = SDL_HAPTIC_INFINITY;
                    effect.constant.direction.type = SDL_HAPTIC_FIRST_AXIS;
                    effect.constant.direction.dir[0] = 0;
                    break;
                case SDL_HAPTIC_SPRING:
                case SDL_HAPTIC_DAMPER:
                    effect.condition.length = SDL_HAPTIC_INFINITY;
                    effect.condition.direction.type = SDL_HAPTIC_FIRST_AXIS;
                    effect.condition.direction.dir[0] = 0;
                    break;
                case SDL_HAPTIC_SINE:
                    effect.periodic.length = SDL_HAPTIC_INFINITY;
                    effect.periodic.direction.type = SDL_HAPTIC_POLAR;
                    effect.periodic.direction.dir[0] = 0;
                    break;
                }
                int effect_id = SDL_HapticNewEffect(haptic, &effect);
                if (effect_id >= 0) {
                    device->force_feedback.haptic = haptic;
                    device->force_feedback.effects |= effect_types[i].type;
                    device->force_feedback.ids[i] = effect_id;
                } else {
                    PRINT_DEBUG_SDL("SDL_HapticNewEffect");
                    if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {
                        fprintf(stderr, "Failed to create %s effect for %s %i\n",
                                effect_types[i].name, SDL_JoystickName(joystick), device->index);
                    }
                }
            }
        }
        if ( !(device->force_feedback.effects & GE_HAPTIC_RUMBLE)
                && (device->force_feedback.effects & GE_HAPTIC_CONSTANT)) {
            SDL_HapticEffect effect = { .type = SDL_HAPTIC_CONSTANT, };
            effect.constant.length = SDL_HAPTIC_INFINITY;
            effect.constant.direction.type = SDL_HAPTIC_POLAR;
            effect.constant.direction.dir[0] = 0;
            int effect_id = SDL_HapticNewEffect(haptic, &effect);
            if (effect_id >= 0) {
                device->force_feedback.haptic = haptic;
                device->force_feedback.effects |= GE_HAPTIC_RUMBLE;
                device->force_feedback.ids[0] = effect_id;
                device->force_feedback.emulate_rumble = GE_HAPTIC_CONSTANT;
            } else {
                PRINT_DEBUG_SDL("SDL_HapticNewEffect");
                if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {
                    fprintf(stderr, "Failed to emulate rumble effect with constant effect for %s %i\n",
                            SDL_JoystickName(joystick), device->index);
                }
            }
        }
        if ( !(device->force_feedback.effects & GE_HAPTIC_RUMBLE)
                && (device->force_feedback.effects & GE_HAPTIC_SINE)) {
            SDL_HapticEffect effect = { .type = SDL_HAPTIC_SINE, };
            effect.periodic.length = SDL_HAPTIC_INFINITY;
            effect.periodic.direction.type = SDL_HAPTIC_POLAR;
            effect.periodic.direction.dir[0] = 0;
            int effect_id = SDL_HapticNewEffect(haptic, &effect);
            if (effect_id >= 0) {
                device->force_feedback.haptic = haptic;
                device->force_feedback.effects |= GE_HAPTIC_RUMBLE;
                device->force_feedback.ids[0] = effect_id;
                device->force_feedback.emulate_rumble = GE_HAPTIC_SINE;
            } else {
                PRINT_DEBUG_SDL("SDL_HapticNewEffect");
                if (GLOG_LEVEL(GLOG_NAME,DEBUG)) {
                    fprintf(stderr, "Failed to emulate rumble effect with sine effect for %s %i\n",
                            SDL_JoystickName(joystick), device->index);
                }
            }
        }
        if (device->force_feedback.effects == GE_HAPTIC_NONE) {
            SDL_HapticClose(haptic);
        }
    }
}

static int js_open(int joystick_index, SDL_GameController ** controller, SDL_Joystick ** joystick) {

    if (SDL_IsGameController(joystick_index)) {
        *controller = SDL_GameControllerOpen(joystick_index);
        if (*controller == NULL) {
            PRINT_ERROR_SDL("SDL_GameControllerOpen");
            return -1;
        }
        *joystick = SDL_GameControllerGetJoystick(*controller);
        if (*joystick == NULL) {
            PRINT_ERROR_SDL("SDL_GameControllerGetJoystick");
            SDL_GameControllerClose(*controller);
            return -1;
        }
    } else {
        *joystick = SDL_JoystickOpen(joystick_index);
        if (*joystick == NULL) {
            PRINT_ERROR_SDL("SDL_JoystickOpen");
            return -1;
        }
    }

    int instanceId = SDL_JoystickInstanceID(*joystick);
    if (instanceId < 0 || (unsigned int) instanceId >= sizeof(sdlInstanceIdToIndex) / sizeof(*sdlInstanceIdToIndex)) {
        if (instanceId < 0) {
            PRINT_ERROR_SDL("SDL_JoystickInstanceID");
        } else {
            PRINT_ERROR_OTHER("instance id is out of bounds");
        }
        if (*controller != NULL) {
            SDL_GameControllerClose(*controller);
        } else {
            SDL_JoystickClose(*joystick);
        }
        return -1;
    }

    return instanceId;
}

static int sdlinput_js_init(const GPOLL_INTERFACE * poll_interface __attribute__((unused)), int (*callback)(GE_Event*)) {

    int i;

    if (callback == NULL) {
        if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
            fprintf(stderr, "callback cannot be NULL\n");
        }
        return -1;
    }

    if (js_init == 0) {
        if (mkb_init == 0) {
            if (SDL_GetHint(SDL_HINT_TIMER_RESOLUTION) == NULL) {
                SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");
            }
            if (SDL_Init(0) < 0) {
                PRINT_ERROR_SDL("SDL_Init");
                return -1;
            }
        }
        js_init = 1;
    }

    if (SDL_InitSubSystem(JOYSTICK_FLAGS) < 0) {
        PRINT_ERROR_SDL("SDL_InitSubSystem");
        return -1;
    }

    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");

    for (i = 0; i < SDL_NumJoysticks(); ++i) {

        struct joystick_device * device = calloc(1, sizeof(*device));
        if (device == NULL) {
            PRINT_ERROR_ALLOC_FAILED("calloc");
            continue;
        }

        SDL_GameController * controller = NULL;
        SDL_Joystick * joystick = NULL;

        int instanceId = js_open(i, &controller, &joystick);
        if (instanceId < 0) {
            free(device);
            continue;
        }

        device->index = js_max_index;
        indexToJoystick[js_max_index] = device;
        sdlInstanceIdToIndex[instanceId] = js_max_index;
        ++js_max_index;

        device->usb_ids.vendor = SDL_JoystickGetDeviceVendor(i);
        device->usb_ids.product = SDL_JoystickGetDeviceProduct(i);
        open_haptic(device, joystick);
        if (controller != NULL) {
            device->controller = controller;
            if (SDL_GameControllerRumble(controller, 0, 0, 0) == 0) {
                device->force_feedback.effects |= GE_HAPTIC_RUMBLE;
                device->force_feedback.hasSimpleRumble = 1;
            }
        } else {
            device->joystick = joystick;
            // query hat info to convert hats to standard buttons
            device->hat_info.joystickHatButtonBaseIndex = SDL_JoystickNumButtons(joystick);
            device->hat_info.joystickNbHat = SDL_JoystickNumHats(joystick);
            if (device->hat_info.joystickNbHat > 0) {
                device->hat_info.joystickHat = calloc(device->hat_info.joystickNbHat, sizeof(unsigned char));
                if (device->hat_info.joystickHat == NULL) {
                    PRINT_ERROR_ALLOC_FAILED("calloc");
                }
            }
        }

        GLIST_ADD(sdl_devices, device);
    }

    joysticks_opened = js_max_index;

    event_js_callback = callback;

    return 0;
}

static int sdlinput_mkb_init(const GPOLL_INTERFACE * poll_interface __attribute__((unused)), int (*callback)(GE_Event*)) {

    if (callback == NULL) {
        if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
            fprintf(stderr, "callback cannot be NULL\n");
        }
        return -1;
    }

    if (mkb_init == 0) {
        if (js_init == 0) {
            if (SDL_GetHint(SDL_HINT_TIMER_RESOLUTION) == NULL) {
                SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");
            }
            if (SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS) == NULL) {
                SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
            }
            if (SDL_Init(0) < 0) {
                PRINT_ERROR_SDL("SDL_Init");
                return -1;
            }
        }
        mkb_init = 1;
    }

    if (SDL_InitSubSystem(MKB_FLAGS) < 0) {
        PRINT_ERROR_SDL("SDL_InitSubSystem");
        return -1;
    }

    window = SDL_CreateWindow(SDLINPUT_WINDOW_NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH,
            SCREEN_HEIGHT, SDL_WINDOW_BORDERLESS);
    if (window == NULL) {
        PRINT_ERROR_SDL("SDL_CreateWindow");
        return -1;
    }

    event_mkb_callback = callback;

    return 0;
}

static void sdlinput_js_quit(void) {

    if (js_init == 0) {
        return;
    }

    GLIST_CLEAN_ALL(sdl_devices, js_close_internal)

    SDL_QuitSubSystem(JOYSTICK_FLAGS);

    if (mkb_init == 0) {
        SDL_Quit();
    }

    js_max_index = 0;

    js_init = 0;
}

static void sdlinput_mkb_quit(void) {

    if (mkb_init == 0) {
        return;
    }

    SDL_DestroyWindow(window);

    SDL_QuitSubSystem(MKB_FLAGS);

    if (js_init == 0) {
        SDL_Quit();
    }

    mkb_init = 0;
}

static const char* sdlinput_js_name(int id) {

    CHECK_JS_DEVICE(id, NULL)

    struct joystick_device * device = indexToJoystick[id];

    if (device->name) {
        return device->name;
    }
    if (device->controller) {
        return SDL_GameControllerName(device->controller);
    }
    return SDL_JoystickName(device->joystick);
}

static int sdlinput_joystick_register(const char* name, unsigned int effects, int (*haptic_cb)(const GE_Event * event)) {

    struct joystick_device * device = calloc(1, sizeof(*device));
    if (device == NULL) {
        PRINT_ERROR_ALLOC_FAILED("calloc");
        return -1;
    }
    device->index = js_max_index;
    device->name = strdup(name);
    device->force_feedback.effects = effects;
    device->force_feedback.haptic_cb = haptic_cb;

    indexToJoystick[js_max_index] = device;

    ++js_max_index;
    ++joysticks_opened;
    ++joysticks_registered;

    GLIST_ADD(sdl_devices, device);

    return device->index;
}

static int js_close_internal(void * user) {

    struct joystick_device * device = (struct joystick_device *) user;

    if (device->name) {
        free(device->name);
        --joysticks_registered;
    } else {
        if (device->force_feedback.haptic) {
            SDL_HapticClose(device->force_feedback.haptic);
        }
        if (device->controller) {
            SDL_GameControllerClose(device->controller);
        } else if (device->joystick) {
            SDL_JoystickClose(device->joystick);
            free(device->hat_info.joystickHat);
        }
    }

    indexToJoystick[device->index] = NULL;

    GLIST_REMOVE(sdl_devices, device);

    free(device);

    --joysticks_opened;

    // Closing the joystick subsystem also closes SDL's event queue.
    // Don't close it if mkb_init is set.
    if (joysticks_opened == joysticks_registered && mkb_init == 0) {
        SDL_QuitSubSystem(JOYSTICK_FLAGS);
    }

    return 0;
}

/*
 * Close a joystick, and close the joystick subsystem if none is used anymore.
 */
static int sdlinput_joystick_close(int id) {

    CHECK_JS_DEVICE(id, -1)

    struct joystick_device * device = indexToJoystick[id];

    return js_close_internal(device);
}

static unsigned int m_num = 0;

static const char* sdlinput_mouse_name(int id) {

    if (id == 0) {
        return "Window Events";
    }
    return NULL;
}

static const char* sdlinput_keyboard_name(int id) {

    if (id == 0) {
        return "Window Events";
    }
    return NULL;
}

static int sdl_peep_events(GE_Event* events, int size);

static int sdlinput_sync_process() {

    /*
     * No joystick is opened and mkb_init is not set.
     */
    if (joysticks_opened == joysticks_registered && mkb_init == 0) {
        return 0;
    }

    unsigned int i;
    int num_evt;
    static GE_Event events[EVENT_BUFFER_SIZE];
    GE_Event* event;

    SDL_PumpEvents();

    num_evt = sdl_peep_events(events, sizeof(events) / sizeof(events[0]));
    
    int ret = 0;

    if (num_evt > 0) {
        for (event = events; event < events + num_evt; ++event) {
            int result = 0;
            switch(event->type) {
            case GE_KEYDOWN:
            case GE_KEYUP:
            case GE_MOUSEMOTION:
            case GE_MOUSEBUTTONDOWN:
            case GE_MOUSEBUTTONUP:
            result = event_mkb_callback(event);
            break;
            case GE_JOYAXISMOTION:
            case GE_JOYBUTTONDOWN:
            case GE_JOYBUTTONUP:
            result = event_js_callback(event);
            break;
            default:
            continue;
            }
            ret |= result;
        }
    }

    for (i = 0; i < m_num; ++i) {
        if (mouse[i].x || mouse[i].y) {
            GE_Event ge = { };
            ge.motion.type = GE_MOUSEMOTION;
            ge.motion.which = i;
            ge.motion.xrel = mouse[i].x;
            ge.motion.yrel = mouse[i].y;
            int result = event_mkb_callback(&ge);
            ret |= result;
            mouse[i].x = 0;
            mouse[i].y = 0;
        }
    }
    
    return ret;
}

static inline int convert_s2g(SDL_Event* se, GE_Event* ge, int size) {

    int nb = 1;
    int index;
    switch (se->type) {
    case SDL_KEYDOWN:
        ge->type = GE_KEYDOWN;
        ge->key.which = 0;
        ge->key.keysym = get_keycode_from_scancode(se->key.keysym.scancode);
        break;
    case SDL_KEYUP:
        ge->type = GE_KEYUP;
        ge->key.which = 0;
        ge->key.keysym = get_keycode_from_scancode(se->key.keysym.scancode);
        break;
    case SDL_MOUSEBUTTONDOWN:
        ge->type = GE_MOUSEBUTTONDOWN;
        ge->button.which = 0;
        ge->button.button = se->button.button;
        break;
    case SDL_MOUSEBUTTONUP:
        ge->type = GE_MOUSEBUTTONUP;
        ge->button.which = 0;
        ge->button.button = se->button.button;
        break;
    case SDL_MOUSEWHEEL:
        if (size >= 2) {
            ge->type = GE_MOUSEBUTTONDOWN;
            ge->button.which = 0;
            if (se->wheel.x > 0) {
                ge->button.button = GE_BTN_WHEELRIGHT;
            } else if (se->wheel.x < 0) {
                ge->button.button = GE_BTN_WHEELLEFT;
            } else if (se->wheel.y > 0) {
                ge->button.button = GE_BTN_WHEELUP;
            } else if (se->wheel.y < 0) {
                ge->button.button = GE_BTN_WHEELDOWN;
            }
            *(ge + 1) = *ge;
            (ge + 1)->type = GE_MOUSEBUTTONUP;
            nb = 2;
        }
        break;
    case SDL_JOYBUTTONDOWN:
        index = sdlInstanceIdToIndex[se->jbutton.which];
        if (indexToJoystick[index]->joystick == NULL) {
            return 0;
        }
        ge->type = GE_JOYBUTTONDOWN;
        ge->jbutton.which = index;
        ge->jbutton.button = se->jbutton.button;
        break;
    case SDL_JOYBUTTONUP:
        index = sdlInstanceIdToIndex[se->jbutton.which];
        if (indexToJoystick[index]->joystick == NULL) {
            return 0;
        }
        ge->type = GE_JOYBUTTONUP;
        ge->jbutton.which = index;
        ge->jbutton.button = se->jbutton.button;
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        ge->type = GE_JOYBUTTONDOWN;
        ge->jbutton.which = sdlInstanceIdToIndex[se->cbutton.which];
        ge->jbutton.button = se->cbutton.button;
        break;
    case SDL_CONTROLLERBUTTONUP:
        ge->type = GE_JOYBUTTONUP;
        ge->jbutton.which = sdlInstanceIdToIndex[se->cbutton.which];
        ge->jbutton.button = se->cbutton.button;
        break;
    case SDL_MOUSEMOTION:
        ge->type = GE_MOUSEMOTION;
        ge->motion.which = 0;
        ge->motion.xrel = se->motion.xrel;
        ge->motion.yrel = se->motion.yrel;
        break;
    case SDL_JOYAXISMOTION:
        index = sdlInstanceIdToIndex[se->jaxis.which];
        if (indexToJoystick[index]->joystick == NULL) {
            return 0;
        }
        ge->type = GE_JOYAXISMOTION;
        ge->jaxis.which = index;
        ge->jaxis.axis = se->jaxis.axis;
        ge->jaxis.value = se->jaxis.value;
        break;
    case SDL_CONTROLLERAXISMOTION:
        ge->type = GE_JOYAXISMOTION;
        ge->jaxis.which = sdlInstanceIdToIndex[se->caxis.which];
        ge->jaxis.axis = se->caxis.axis;
        ge->jaxis.value = se->caxis.value;
        break;
    case SDL_JOYHATMOTION:
        index = sdlInstanceIdToIndex[se->jhat.which];
        if (indexToJoystick[index]->joystick == NULL) {
            return 0;
        }
        ge->type = GE_JOYHATMOTION;
        ge->jhat.which = index;
        ge->jhat.hat = se->jhat.hat;
        ge->jhat.value = se->jhat.value;
        break;
    default:
        return 0;
    }
    return nb;
}

static int joystick_hat_button(GE_Event* event, unsigned char hat_dir) {

    return indexToJoystick[event->jhat.which]->hat_info.joystickHatButtonBaseIndex + 4 * event->jhat.hat + log2(hat_dir);
}

static unsigned char get_joystick_hat(GE_Event* event) {

    if (event->jhat.hat < indexToJoystick[event->jhat.which]->hat_info.joystickNbHat) {
        return indexToJoystick[event->jhat.which]->hat_info.joystickHat[event->jhat.hat];
    }
    return 0;
}

static void set_joystick_hat(GE_Event* event) {

    if (event->jhat.hat < indexToJoystick[event->jhat.which]->hat_info.joystickNbHat) {
        indexToJoystick[event->jhat.which]->hat_info.joystickHat[event->jhat.hat] = event->jhat.value;
    }
}

/*
 * This function translates joystick hat events into joystick button events.
 * The joystick button events are inserted just before the joystick hat events.
 */
static int hats_to_buttons(GE_Event *events, int numevents) {

    GE_Event* event;
    unsigned char hat_dir;

    if (numevents == EVENT_BUFFER_SIZE) {
        return EVENT_BUFFER_SIZE;
    }

    for (event = events; event < events + numevents; ++event) {
        switch (event->type) {
        case GE_JOYHATMOTION:
            /*
             * Check what hat directions changed.
             * The new hat state is compared to the previous one.
             */
            for (hat_dir = 1; hat_dir < 16 && event < events + numevents; hat_dir *= 2) {
                if (event->jhat.value & hat_dir) {
                    if (!(get_joystick_hat(event) & hat_dir)) {
                        /*
                         * The hat direction is pressed.
                         */
                        memmove(event + 1, event, (events + numevents - event) * sizeof(GE_Event));
                        event->type = GE_JOYBUTTONDOWN;
                        event->jbutton.which = (event + 1)->jhat.which;
                        event->jbutton.button = joystick_hat_button((event + 1), hat_dir);
                        event++;
                        numevents++;
                        if (numevents == EVENT_BUFFER_SIZE) {
                            return EVENT_BUFFER_SIZE;
                        }
                    }
                } else {
                    if (get_joystick_hat(event) & hat_dir) {
                        /*
                         * The hat direction is released.
                         */
                        memmove(event + 1, event, (events + numevents - event) * sizeof(GE_Event));
                        event->type = GE_JOYBUTTONUP;
                        event->jbutton.which = (event + 1)->jhat.which;
                        event->jbutton.button = joystick_hat_button((event + 1), hat_dir);
                        event++;
                        numevents++;
                        if (numevents == EVENT_BUFFER_SIZE) {
                            return EVENT_BUFFER_SIZE;
                        }
                    }
                }
            }
            /*
             * Save the new hat state.
             */
            set_joystick_hat(event);
            /*
             * Remove the joystick hat event.
             */
            memmove(event, event + 1, (events + numevents - event - 1) * sizeof(GE_Event));
            event--;
            numevents--;
            break;
        default:
            break;
        }
    }
    return numevents;
}

static int sdl_peep_events(GE_Event* events, int size) {

    static SDL_Event sdl_events[EVENT_BUFFER_SIZE];

    int i, j;

    if (size > EVENT_BUFFER_SIZE) {
        size = EVENT_BUFFER_SIZE;
    }

    unsigned int minType = SDL_JOYAXISMOTION;
    unsigned int maxType = SDL_CONTROLLERDEVICEREMAPPED;

    if (mkb_init != 0) {
        minType = SDL_KEYDOWN;
    }

    if (js_init == 0) {
        maxType = SDL_MOUSEWHEEL;
    }

    if (minType > maxType) {
        return 0;
    }

    int nb = SDL_PeepEvents(sdl_events, size, SDL_GETEVENT, minType, maxType);

    if (nb < 0) {
        PRINT_ERROR_SDL("SDL_PeepEvents");
        return -1;
    }

    j = 0;
    for (i = 0; i < nb && j < size; ++i) {
        j += convert_s2g(sdl_events + i, events + j, size - j);
    }

    return hats_to_buttons(events, j);
}

static int sdlinput_joystick_get_haptic(int joystick) {

    if (joystick < 0 || joystick >= js_max_index || indexToJoystick[joystick] == NULL) {
        return -1;
    }
    return indexToJoystick[joystick]->force_feedback.effects;
}

static int sdlinput_joystick_set_haptic(const GE_Event * event) {

    if (event->which >= js_max_index || indexToJoystick[event->which] == NULL) {
        PRINT_ERROR_OTHER("Invalid joystick id.");
        return -1;
    }
    struct joystick_device * joystick = indexToJoystick[event->which];
    if (joystick->controller == NULL && joystick->joystick == NULL) {
        if (joystick->force_feedback.haptic_cb != NULL) {
            return joystick->force_feedback.haptic_cb(event);
        } else {
            PRINT_ERROR_OTHER("External joystick has no haptic callback.");
            return -1;
        }
    }

    if (event->type == GE_JOYRUMBLE && joystick->force_feedback.hasSimpleRumble) {
        if (SDL_GameControllerRumble(joystick->controller, event->jrumble.strong, event->jrumble.weak, 0)) {
            PRINT_ERROR_SDL("SDL_GameControllerRumble");
            return -1;
        }
        return 0;
    }

    int effect_id = -1;
    SDL_HapticEffect effect = { };
    unsigned int effects = joystick->force_feedback.effects;
    switch (event->type) {
    case GE_JOYRUMBLE:
        if (effects & GE_HAPTIC_RUMBLE) {
            effect_id = get_effect_id(joystick, GE_HAPTIC_RUMBLE);
            if (joystick->force_feedback.emulate_rumble == GE_HAPTIC_NONE) {
                effect.leftright.type = SDL_HAPTIC_LEFTRIGHT;
                effect.leftright.length = SDL_HAPTIC_INFINITY;
                effect.leftright.large_magnitude = event->jrumble.strong;
                effect.leftright.small_magnitude = event->jrumble.weak;
            } else if (joystick->force_feedback.emulate_rumble == GE_HAPTIC_SINE) {
                effect.periodic.type = SDL_HAPTIC_SINE;
                effect.periodic.direction.type = SDL_HAPTIC_POLAR;
                if (event->jrumble.strong != 0) {
                    effect.periodic.direction.dir[0] = atan(event->jrumble.weak / event->jrumble.strong) * 100;
                } else if (event->jrumble.weak != 0) {
                    effect.periodic.direction.dir[0] = 9000;
                }
                effect.periodic.length = SDL_HAPTIC_INFINITY;
                effect.periodic.period = 0;
                effect.periodic.magnitude = 0;
                effect.periodic.offset = hypot(event->jrumble.strong, event->jrumble.weak);
            } else if (joystick->force_feedback.emulate_rumble == GE_HAPTIC_CONSTANT) {
                effect.constant.type = SDL_HAPTIC_CONSTANT;
                effect.constant.direction.type = SDL_HAPTIC_POLAR;
                if (event->jrumble.strong != 0) {
                    effect.constant.direction.dir[0] = atan(event->jrumble.weak / event->jrumble.strong) * 100;
                } else if (event->jrumble.weak != 0) {
                    effect.constant.direction.dir[0] = 9000;
                }
                effect.constant.length = SDL_HAPTIC_INFINITY;
                effect.constant.level = hypot(event->jrumble.strong, event->jrumble.weak);
            }
        }
        break;
    case GE_JOYCONSTANTFORCE:
        if (effects & GE_HAPTIC_CONSTANT) {
            effect_id = get_effect_id(joystick, GE_HAPTIC_CONSTANT);
            effect.constant.type = SDL_HAPTIC_CONSTANT;
            effect.constant.direction.type = SDL_HAPTIC_FIRST_AXIS;
            effect.constant.direction.dir[0] = 0;
            effect.constant.length = SDL_HAPTIC_INFINITY;
            effect.constant.level = event->jconstant.level;
        }
        break;
    case GE_JOYSPRINGFORCE:
        if (effects & GE_HAPTIC_SPRING) {
            effect_id = get_effect_id(joystick, GE_HAPTIC_SPRING);
            effect.condition.type = SDL_HAPTIC_SPRING;
            effect.condition.direction.type = SDL_HAPTIC_FIRST_AXIS;
            effect.condition.direction.dir[0] = 0;
            effect.condition.length = SDL_HAPTIC_INFINITY;
            effect.condition.right_sat[0] = event->jcondition.saturation.right;
            effect.condition.left_sat[0] = event->jcondition.saturation.left;
            effect.condition.right_coeff[0] = event->jcondition.coefficient.right;
            effect.condition.left_coeff[0] = event->jcondition.coefficient.left;
            effect.condition.center[0] = event->jcondition.center;
            effect.condition.deadband[0] = event->jcondition.deadband;
        }
        break;
    case GE_JOYDAMPERFORCE:
        if (effects & GE_HAPTIC_DAMPER) {
            effect_id = get_effect_id(joystick, GE_HAPTIC_DAMPER);
            effect.condition.type = SDL_HAPTIC_DAMPER;
            effect.condition.direction.type = SDL_HAPTIC_FIRST_AXIS;
            effect.condition.direction.dir[0] = 0;
            effect.condition.length = SDL_HAPTIC_INFINITY;
            effect.condition.right_sat[0] = event->jcondition.saturation.right;
            effect.condition.left_sat[0] = event->jcondition.saturation.left;
            effect.condition.right_coeff[0] = event->jcondition.coefficient.right;
            effect.condition.left_coeff[0] = event->jcondition.coefficient.left;
        }
        break;
    case GE_JOYSINEFORCE:
        if (effects & GE_HAPTIC_SINE) {
            effect_id = get_effect_id(joystick, GE_HAPTIC_SINE);
            effect.periodic.type = SDL_HAPTIC_SINE;
            effect.periodic.direction.type = SDL_HAPTIC_POLAR;
            effect.periodic.direction.dir[0] = event->jperiodic.sine.direction;
            effect.periodic.length = SDL_HAPTIC_INFINITY;
            effect.periodic.period = event->jperiodic.sine.period;
            effect.periodic.magnitude =  event->jperiodic.sine.magnitude;
            effect.periodic.offset =  event->jperiodic.sine.offset;
        }
        break;
    default:
        break;
    }
    if (effect_id != -1) {
        if (SDL_HapticUpdateEffect(joystick->force_feedback.haptic, effect_id, &effect)) {
            PRINT_ERROR_SDL("SDL_HapticUpdateEffect");
            return -1;
        }
        if (SDL_HapticRunEffect(joystick->force_feedback.haptic, effect_id, 1)) {
            PRINT_ERROR_SDL("SDL_HapticRunEffect");
            return -1;
        }
    }
    return 0;
}

static int sdlinput_joystick_get_usb_ids(int joystick, unsigned short * vendor, unsigned short * product) {

    if (joystick < 0 || joystick >= js_max_index || indexToJoystick[joystick] == NULL) {
        return -1;
    }
    *vendor = indexToJoystick[joystick]->usb_ids.vendor;
    *product = indexToJoystick[joystick]->usb_ids.product;
    return 0;
}

static int sdlinput_grab(int mode) {

    if (SDL_SetRelativeMouseMode((mode == GE_GRAB_ON) ? SDL_TRUE : SDL_FALSE)) {
        PRINT_ERROR_SDL("SDL_SetRelativeMouseMode");
    }

    return mode;
}

static int sdlinput_get_src() {

    return GE_MKB_SOURCE_WINDOW_SYSTEM;
}

static struct js_source sdl_js_source = {
    .init = sdlinput_js_init,
    .get_name = sdlinput_js_name,
    .add = sdlinput_joystick_register,
    .get_haptic = sdlinput_joystick_get_haptic,
    .set_haptic = sdlinput_joystick_set_haptic,
    .get_hid = NULL,
    .get_usb_ids = sdlinput_joystick_get_usb_ids,
    .close = sdlinput_joystick_close,
    .sync_process = sdlinput_sync_process,
    .quit = sdlinput_js_quit,
};

static struct mkb_source sdl_mkb_source = {
    .init = sdlinput_mkb_init,
    .get_src = sdlinput_get_src,
    .grab = sdlinput_grab,
    .get_mouse_name = sdlinput_mouse_name,
    .get_keyboard_name = sdlinput_keyboard_name,
    .sync_process = sdlinput_sync_process,
    .quit = sdlinput_mkb_quit,
};

void sdlinput_constructor() __attribute__((constructor));
void sdlinput_constructor() {
    ev_register_js_source(&sdl_js_source);
    ev_register_mkb_source(&sdl_mkb_source);
}
