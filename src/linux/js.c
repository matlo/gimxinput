/*
 Copyright (c) 2013 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <stdio.h>
#include <linux/joystick.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <ginput.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxcommon/include/gerror.h>
#include <gimxcommon/include/glist.h>
#include <gimxlog/include/glog.h>
#include "../events.h"

#define eprintf(...) if(debug) printf(__VA_ARGS__)

GLOG_GET(GLOG_NAME)

static int debug = 0;

#define AXMAP_SIZE (ABS_MAX + 1)

static GPOLL_REMOVE_SOURCE fp_remove = NULL;

static struct {
    unsigned char jstype;
    GE_HapticType type;
} effect_types[] = {
        { FF_RUMBLE,   GE_HAPTIC_RUMBLE },
        { FF_CONSTANT, GE_HAPTIC_CONSTANT },
        { FF_SPRING,   GE_HAPTIC_SPRING },
        { FF_DAMPER,   GE_HAPTIC_DAMPER },
        { FF_PERIODIC, GE_HAPTIC_SINE }
};

struct joystick_device {
    int id; // the id of the joystick in the generated events
    int fd; // the opened joystick, or -1 in case the joystick was created using the js_add() function
    char* name; // the name of the joystick
    int isSixaxis;
    struct {
        unsigned short button_nb; // the base index of the generated hat buttons equals the number of physical buttons
        int hat_value[ABS_HAT3Y - ABS_HAT0X]; // the current hat values
        uint8_t ax_map[AXMAP_SIZE]; // the axis map
    } hat_info; // allows to convert hat axes to buttons
    struct {
        int fd; // the event device, or -1 in case the joystick was created using the js_add() function
        unsigned int effects;
        int ids[sizeof(effect_types) / sizeof(*effect_types)];
        int constant_id;
        int spring_id;
        int damper_id;
        int (*haptic_cb)(const GE_Event * event);
    } force_feedback;
    void * hid;
    GLIST_LINK(struct joystick_device);
};

static struct joystick_device * indexToJoystick[GE_MAX_DEVICES] = { };

static int j_num; // the number of joysticks

#define CHECK_DEVICE(INDEX, RETVALUE) \
    if(INDEX < 0 || INDEX >= j_num || indexToJoystick[INDEX] == NULL) \
    { \
      return RETVALUE; \
    }

static int js_close_internal(void * user);

static GLIST_INST(struct joystick_device, js_devices);

int get_effect_id(struct joystick_device * device, GE_HapticType type) {
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

static int (*event_callback)(GE_Event*) = NULL;

static void js_process_event(struct joystick_device * device, struct js_event* je) {
    GE_Event evt = { };

    if (je->type & JS_EVENT_INIT) {
        return;
    }

    if (je->type & JS_EVENT_BUTTON) {
        evt.type = je->value ? GE_JOYBUTTONDOWN : GE_JOYBUTTONUP;
        evt.jbutton.which = device->id;
        evt.jbutton.button = je->number;
    } else if (je->type & JS_EVENT_AXIS) {
        int axis = device->hat_info.ax_map[je->number];
        if (axis >= ABS_HAT0X && axis <= ABS_HAT3Y) {
            // convert hat axes to buttons
            evt.type = je->value ? GE_JOYBUTTONDOWN : GE_JOYBUTTONUP;
            int button;
            int value;
            axis -= ABS_HAT0X;
            if (!je->value) {
                value = device->hat_info.hat_value[axis];
                device->hat_info.hat_value[axis] = 0;
            } else {
                value = je->value / 32767;
                device->hat_info.hat_value[axis] = value;
            }
            button = axis + value + 2 * (axis / 2);
            if (button < 4 * (axis / 2)) {
                button += 4;
            }
            evt.jbutton.which = device->id;
            evt.jbutton.button = button + device->hat_info.button_nb;
        } else {
            evt.type = GE_JOYAXISMOTION;
            evt.jaxis.which = device->id;
            evt.jaxis.axis = je->number;
            evt.jaxis.value = je->value;
            /*
             * Ugly patch for the sixaxis.
             */
            if (device->isSixaxis && evt.jaxis.axis > 3 && evt.jaxis.axis < 23) {
                evt.jaxis.value = (evt.jaxis.value + 32767) / 2;
            }
        }
    }

    /*
     * Process evt.
     */
    if (evt.type != GE_NOEVENT) {
        eprintf("event from joystick: %s\n", device->name);
        eprintf("type: %d number: %d value: %d\n", je->type, je->number, je->value);
        event_callback(&evt);
    }
}

static int js_process_events(void * user) {

    struct joystick_device * device = (struct joystick_device *) user;

    static struct js_event je[MAX_EVENTS];

    int res = read(device->fd, je, sizeof(je));
    if (res > 0) {
        unsigned int j;
        for (j = 0; j < res / sizeof(*je); ++j) {
            js_process_event(device, je + j);
        }
    } else if (res < 0 && errno != EAGAIN) {
        js_close_internal(device);
    }

    return 0;
}

#define DEV_INPUT "/dev/input"
#define JS_DEV_NAME "js%u"
#define EV_DEV_NAME "event%u"

#define BITS_PER_LONG (sizeof(long) * 8)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

static int is_js_device(const struct dirent *dir) {

    unsigned int num;
    if (dir->d_type == DT_CHR && sscanf(dir->d_name, JS_DEV_NAME, &num) == 1 && num < 256) {
        return 1;
    }
    return 0;
}

static int is_event_dir(const struct dirent *dir) {

    unsigned int num;
    if (dir->d_type == DT_DIR && sscanf(dir->d_name, EV_DEV_NAME, &num) == 1 && num < 256) {
        return 1;
    }
    return 0;
}

static int open_evdev(const char * js_name) {

    struct dirent **namelist_ev;
    int n_ev;
    int j;
    int fd_ev = -1;

    char dir_event[strlen("/sys/class/input/") + strlen(js_name) + strlen("/device/") + 1];
    snprintf(dir_event, sizeof(dir_event), "/sys/class/input/%s/device/", js_name);

    // scan /sys/class/input/jsX/device/ for eventY devices
    n_ev = scandir(dir_event, &namelist_ev, is_event_dir, alphasort);
    if (n_ev >= 0) {
        for (j = 0; j < n_ev; ++j) {
            if (fd_ev == -1) {
                char event[strlen(DEV_INPUT) + sizeof('/') + strlen(namelist_ev[j]->d_name) + 1];
                snprintf(event, sizeof(event), "%s/%s", DEV_INPUT, namelist_ev[j]->d_name);
                // open the eventY device
                fd_ev = open(event, O_RDWR | O_NONBLOCK);
            }
            free(namelist_ev[j]);
        }
        free(namelist_ev);
    }
    return fd_ev;
}

static void * get_hid(int fd_ev) {

    char uniq[64] = { };
    if (ioctl(fd_ev, EVIOCGUNIQ(sizeof(uniq)), &uniq) == -1) {
        return NULL;
    }
    pid_t pid;
    void * hid;
    if (sscanf(uniq, "GIMX %d %p", &pid, &hid) == 2) {
        if (pid == getpid()) {
            return hid;
        }
    }
    return NULL;
}

static int open_haptic(struct joystick_device * device, int fd_ev) {

    unsigned long features[4];
    if (ioctl(fd_ev, EVIOCGBIT(EV_FF, sizeof(features)), features) == -1) {
        PRINT_ERROR_ERRNO("ioctl EV_FF");
        return -1;
    }
    unsigned int i;
    for (i = 0; i < sizeof(effect_types) / sizeof(*effect_types); ++i) {
        if (test_bit(effect_types[i].jstype, features)) {
            // Upload the effect.
            struct ff_effect effect = { .type = effect_types[i].jstype, .id = -1 };
            if (effect_types[i].type == GE_HAPTIC_SINE) {
                effect.u.periodic.waveform = FF_SINE;
            }
            if (ioctl(fd_ev, EVIOCSFF, &effect) != -1) {
                // Store the id so that the effect can be updated and played later.
                device->force_feedback.fd = fd_ev;
                device->force_feedback.effects |= effect_types[i].type;
                device->force_feedback.ids[i] = effect.id;
            } else {
                PRINT_ERROR_ERRNO("ioctl EVIOCSFF");
            }
        }
    }
    if (device->force_feedback.effects == GE_HAPTIC_NONE) {
        return -1;
    }
    return 0;
}

#define SIXAXIS_NAME "Sony PLAYSTATION(R)3 Controller"
#define NAVIGATION_NAME "Sony Navigation Controller"
#define BT_SIXAXIS_NAME "PLAYSTATION(R)3 Controller" // QtSixa name prefix (end contains the bdaddr)

int isSixaxis(const char * name) {

    if (!strncmp(name, SIXAXIS_NAME, sizeof(SIXAXIS_NAME))) {
        return 1;
    } else if (!strncmp(name, NAVIGATION_NAME, sizeof(NAVIGATION_NAME))) {
        return 1;
    } else if (!strncmp(name, BT_SIXAXIS_NAME, sizeof(BT_SIXAXIS_NAME) - 1)) {
        return 1;
    }
    return 0;
}

static int js_init(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*)) {

    int ret = 0;
    int i;
    int fd_js;
    char name[1024] = { 0 };

    struct dirent **namelist_js;
    int n_js;

    if (poll_interface->fp_register == NULL) {
        PRINT_ERROR_OTHER("fp_register is NULL");
        return -1;
    }

    if (poll_interface->fp_remove == NULL) {
        PRINT_ERROR_OTHER("fp_remove is NULL");
        return -1;
    }

    if (callback == NULL) {
        PRINT_ERROR_OTHER("callback is NULL");
        return -1;
    }

    event_callback = callback;
    fp_remove = poll_interface->fp_remove;

    // scan /dev/input for jsX devices
    n_js = scandir(DEV_INPUT, &namelist_js, is_js_device, alphasort);
    if (n_js >= 0) {
        for (i = 0; i < n_js; ++i) {
#define JSINIT_ERROR() \
      close(fd_js); \
      free(namelist_js[i]); \
      continue;

            if (j_num == sizeof(indexToJoystick) / sizeof(*indexToJoystick)) {
                PRINT_ERROR_OTHER("cannot add other joysticks: max device number reached");
                free(namelist_js[i]);
                continue;
            }

            char js_file[strlen(DEV_INPUT) + sizeof('/') + strlen(namelist_js[i]->d_name) + 1];
            snprintf(js_file, sizeof(js_file), "%s/%s", DEV_INPUT, namelist_js[i]->d_name);

            // open the jsX device
            fd_js = open(js_file, O_RDONLY | O_NONBLOCK);
            if (fd_js != -1) {
                // get the device name
                if (ioctl(fd_js, JSIOCGNAME(sizeof(name) - 1), name) < 0) {
                    PRINT_ERROR_ERRNO("ioctl EVIOCGNAME");
                    JSINIT_ERROR()
                }
                // get the number of buttons and the axis map, to allow converting hat axes to buttons
                unsigned char buttons;
                if (ioctl(fd_js, JSIOCGBUTTONS, &buttons) < 0) {
                    JSINIT_ERROR()
                }
                uint8_t ax_map[AXMAP_SIZE] = {};
                if (ioctl(fd_js, JSIOCGAXMAP, &ax_map) < 0) {
                    JSINIT_ERROR()
                }
                struct joystick_device * device = calloc(1, sizeof(*device));
                if (device == NULL) {
                    PRINT_ERROR_ALLOC_FAILED("calloc");
                    JSINIT_ERROR()
                }
                device->id = j_num;
                indexToJoystick[j_num] = device;
                device->name = strdup(name);
                device->isSixaxis = isSixaxis(name);
                device->fd = fd_js;
                device->force_feedback.fd = -1;
                device->hat_info.button_nb = buttons;
                memcpy(device->hat_info.ax_map, ax_map, sizeof(device->hat_info.ax_map));
                GPOLL_CALLBACKS callbacks = { .fp_read = js_process_events, .fp_write = NULL, .fp_close =
                        js_close_internal };
                poll_interface->fp_register(device->fd, device, &callbacks);
                int fd_ev = open_evdev(namelist_js[i]->d_name);
                if (fd_ev >= 0) {
                    device->hid = get_hid(fd_ev);
                    if (open_haptic(device, fd_ev) == -1) {
                        close(fd_ev); //no need to keep it opened
                    }
                }
                GLIST_ADD(js_devices, device);
                j_num++;
            } else {
                if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
                    fprintf(stderr, "%s:%d %s: opening %s failed with error: %m\n", __FILE__, __LINE__, __func__, js_file);
                }
            }

            free(namelist_js[i]);
        }
        free(namelist_js);
    } else {
        if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
            fprintf(stderr, "can't scan directory %s: %s\n", DEV_INPUT, strerror(errno));
        }
        ret = -1;
    }

    return ret;
}

static int js_get_haptic(int joystick) {

    CHECK_DEVICE(joystick, -1)

    return indexToJoystick[joystick]->force_feedback.effects;
}

static int js_set_haptic(const GE_Event * event) {

    int joystick = event->which;

    CHECK_DEVICE(joystick, -1)

    struct joystick_device * device = indexToJoystick[event->which];

    int ret = 0;

    int fd = device->force_feedback.fd;

    if (fd >= 0) {
        struct ff_effect effect = { .id = -1, .direction = 0x4000 /* positive means left */};
        unsigned int effects = device->force_feedback.effects;
        switch (event->type) {
        case GE_JOYRUMBLE:
            if (effects & GE_HAPTIC_RUMBLE) {
                effect.id = get_effect_id(device, GE_HAPTIC_RUMBLE);
                effect.type = FF_RUMBLE;
                effect.u.rumble.strong_magnitude = event->jrumble.strong;
                effect.u.rumble.weak_magnitude = event->jrumble.weak;
            }
            break;
        case GE_JOYCONSTANTFORCE:
            if (effects & GE_HAPTIC_CONSTANT) {
                effect.id = get_effect_id(device, GE_HAPTIC_CONSTANT);
                effect.type = FF_CONSTANT;
                effect.u.constant.level = event->jconstant.level;
            }
            break;
        case GE_JOYSPRINGFORCE:
            if (effects & GE_HAPTIC_SPRING) {
                effect.id = get_effect_id(device, GE_HAPTIC_SPRING);
                effect.type = FF_SPRING;
                effect.u.condition[0].right_saturation = event->jcondition.saturation.right;
                effect.u.condition[0].left_saturation = event->jcondition.saturation.left;
                effect.u.condition[0].right_coeff = event->jcondition.coefficient.right;
                effect.u.condition[0].left_coeff = event->jcondition.coefficient.left;
                effect.u.condition[0].center = event->jcondition.center;
                effect.u.condition[0].deadband = event->jcondition.deadband;
            }
            break;
        case GE_JOYDAMPERFORCE:
            if (effects & GE_HAPTIC_DAMPER) {
                effect.id = get_effect_id(device, GE_HAPTIC_DAMPER);
                effect.type = FF_DAMPER;
                effect.u.condition[0].right_saturation = event->jcondition.saturation.right;
                effect.u.condition[0].left_saturation = event->jcondition.saturation.left;
                effect.u.condition[0].right_coeff = event->jcondition.coefficient.right;
                effect.u.condition[0].left_coeff = event->jcondition.coefficient.left;
                effect.u.condition[0].center = event->jcondition.center;
                effect.u.condition[0].deadband = event->jcondition.deadband;
            }
            break;
        case GE_JOYSINEFORCE:
            if (effects & GE_HAPTIC_SINE) {
                effect.id = get_effect_id(device, GE_HAPTIC_SINE);
                effect.type = FF_PERIODIC;
                effect.u.periodic.waveform = FF_SINE;
                effect.u.periodic.magnitude = event->jperiodic.sine.magnitude;
                effect.u.periodic.offset = event->jperiodic.sine.offset;
                effect.u.periodic.period = event->jperiodic.sine.period;
            }
            break;
        default:
            break;
        }
        if (effect.id != -1) {
            // Update the effect.
            if (ioctl(fd, EVIOCSFF, &effect) == -1) {
                PRINT_ERROR_ERRNO("ioctl EVIOCSFF");
                ret = -1;
            }
            struct input_event play = { .type = EV_FF, .value = 1, /* play: 1, stop: 0 */
            .code = effect.id };
            // Play the effect.
            if (write(fd, (const void*) &play, sizeof(play)) == -1) {
                PRINT_ERROR_ERRNO("write");
                ret = -1;
            }
        }
    } else if (device->force_feedback.haptic_cb) {
        ret = device->force_feedback.haptic_cb(event);
    } else {
        ret = -1;
    }

    return ret;
}

static void * js_get_hid(int joystick) {

    CHECK_DEVICE(joystick, NULL)

    return indexToJoystick[joystick]->hid;
}

static int js_close_internal(void * user) {

    struct joystick_device * device = (struct joystick_device *) user;

    free(device->name);

    if (device->fd >= 0) {
        fp_remove(device->fd);
        close(device->fd);
    }
    if (device->force_feedback.fd >= 0) {
        close(device->force_feedback.fd);
    }

    indexToJoystick[device->id] = NULL;

    GLIST_REMOVE(js_devices, device);

    free(device);

    return 0;
}

static int js_close(int joystick) {

    CHECK_DEVICE(joystick, -1)

    return js_close_internal(indexToJoystick[joystick]);
}

static void js_quit() {

    GLIST_CLEAN_ALL(js_devices, js_close_internal)

    j_num = 0;
}

static const char* js_get_name(int joystick) {

    CHECK_DEVICE(joystick, NULL)

    return indexToJoystick[joystick]->name;
}

static int js_add(const char * name, unsigned int effects, int (*haptic_cb)(const GE_Event * event)) {

    int index = -1;
    if (j_num < GE_MAX_DEVICES) {
        struct joystick_device * device = calloc(1, sizeof(*device));
        if (device != NULL) {
            index = j_num;
            indexToJoystick[j_num] = device;
            device->id = index;
            device->fd = -1;
            device->name = strdup(name);
            device->force_feedback.fd = -1;
            device->force_feedback.effects = effects;
            device->force_feedback.haptic_cb = haptic_cb;
            GLIST_ADD(js_devices, device);
            ++j_num;
        } else {
            PRINT_ERROR_ALLOC_FAILED("calloc");
        }
    }
    return index;
}

static struct js_source source = {
    .init = js_init,
    .get_name = js_get_name,
    .add = js_add,
    .get_haptic = js_get_haptic,
    .set_haptic = js_set_haptic,
    .get_hid = js_get_hid,
    .close = js_close,
    .sync_process = NULL,
    .quit = js_quit,
};

void js_constructor() __attribute__((constructor));
void js_constructor() {
    ev_register_js_source(&source);
}
