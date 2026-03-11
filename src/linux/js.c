/*
 Copyright (c) 2013 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/input.h>
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

static int scale_axis(int fd, int axis_code, int raw_value) {
    struct input_absinfo absinfo;
    if (ioctl(fd, EVIOCGABS(axis_code), &absinfo) < 0) {
        return raw_value;
    }
    int min_val = absinfo.minimum;
    int max_val = absinfo.maximum;
    int center = (min_val + max_val) / 2;
    int half_range = (max_val - min_val) / 2;
    if (half_range <= 0) {
        return 0;
    }
    int scaled = (raw_value - center) * 32767 / half_range;
    if (scaled < -32767) scaled = -32767;
    if (scaled > 32767) scaled = 32767;
    return scaled;
}

static void evdev_process_event(struct joystick_device * device, struct input_event * ie) {
    GE_Event evt = { };

    switch (ie->type) {
    case EV_KEY:
        if (ie->value > 1) {
            return;
        }
        evt.type = ie->value ? GE_JOYBUTTONDOWN : GE_JOYBUTTONUP;
        evt.jbutton.which = device->id;
        evt.jbutton.button = ie->code;
        break;

    case EV_ABS: {
        int axis = ie->code;
        if (axis >= ABS_HAT0X && axis <= ABS_HAT3Y) {
            /* convert hat axes to buttons */
            evt.type = ie->value ? GE_JOYBUTTONDOWN : GE_JOYBUTTONUP;
            int value;
            axis -= ABS_HAT0X;
            if (!ie->value) {
                value = device->hat_info.hat_value[axis];
                device->hat_info.hat_value[axis] = 0;
            } else {
                value = ie->value > 0 ? 1 : -1;
                device->hat_info.hat_value[axis] = value;
            }
            int button = axis + value + 2 * (axis / 2);
            if (button < 4 * (axis / 2)) {
                button += 4;
            }
            evt.jbutton.which = device->id;
            evt.jbutton.button = button + device->hat_info.button_nb;
        } else {
            evt.type = GE_JOYAXISMOTION;
            evt.jaxis.which = device->id;
            evt.jaxis.axis = axis;
            evt.jaxis.value = scale_axis(device->fd, axis, ie->value);
        }
        break;
    }

    default:
        return;
    }

    if (evt.type != GE_NOEVENT) {
        eprintf("event from joystick: %s\n", device->name);
        eprintf("type: %d code: %d value: %d\n", ie->type, ie->code, ie->value);
        event_callback(&evt);
    }
}

static int js_process_events(void * user) {

    struct joystick_device * device = (struct joystick_device *) user;

    static struct input_event ie[MAX_EVENTS];

    int res = read(device->fd, ie, sizeof(ie));
    if (res > 0) {
        unsigned int j;
        for (j = 0; j < (unsigned int)res / sizeof(*ie); ++j) {
            evdev_process_event(device, ie + j);
        }
    } else if (res < 0 && errno != EAGAIN) {
        js_close_internal(device);
    }

    return 0;
}

#define DEV_INPUT "/dev/input"
#define SYS_INPUT "/sys/class/input"
#define JS_DEV_NAME "js%u"
#define EV_DEV_NAME "event%u"

#define BITS_PER_LONG (sizeof(long) * 8)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

static int is_js_device(const struct dirent *dir) {

    unsigned int num;
    if (dir->d_type == DT_CHR && sscanf(dir->d_name, JS_DEV_NAME, &num) == 1 && num < 4096) {
        return 1;
    }
    return 0;
}

static int is_event_device(const struct dirent *dir) {

    unsigned int num;
    if (dir->d_type == DT_DIR && sscanf(dir->d_name, EV_DEV_NAME, &num) == 1 && num < 4096) {
        return 1;
    }
    return 0;
}

/* Accept DT_DIR, DT_LNK, DT_UNKNOWN for event entries in sysfs (symlinks common) */
static int is_event_in_device(const struct dirent *dir) {
    unsigned int num;
    if ((dir->d_type == DT_DIR || dir->d_type == DT_LNK || dir->d_type == DT_UNKNOWN) &&
        sscanf(dir->d_name, EV_DEV_NAME, &num) == 1 && num < 4096) {
        return 1;
    }
    return 0;
}

/* Check if event_name (e.g. "event5") is under any js device's device directory */
static int is_event_under_js(const char * event_name) {

    struct dirent **namelist_js;
    int n_js = scandir(SYS_INPUT, &namelist_js, is_js_device, alphasort);
    if (n_js < 0) {
        return 0;
    }
    int found = 0;
    for (int i = 0; i < n_js && !found; ++i) {
        char dir_event[strlen(SYS_INPUT) + 1 + strlen(namelist_js[i]->d_name) + strlen("/device/") + 1];
        snprintf(dir_event, sizeof(dir_event), "%s/%s/device/", SYS_INPUT, namelist_js[i]->d_name);
        struct dirent **namelist_ev;
        int n_ev = scandir(dir_event, &namelist_ev, is_event_in_device, alphasort);
        if (n_ev >= 0) {
            for (int j = 0; j < n_ev && !found; ++j) {
                if (strcmp(namelist_ev[j]->d_name, event_name) == 0) {
                    found = 1;
                }
                free(namelist_ev[j]);
            }
            free(namelist_ev);
        }
        free(namelist_js[i]);
    }
    free(namelist_js);
    return found;
}

static int open_evdev(const char * js_name) {

    struct dirent **namelist_ev;
    int n_ev;
    int j;
    int fd_ev = -1;

    char dir_event[strlen(SYS_INPUT) + 1 + strlen(js_name) + strlen("/device/") + 1];
    snprintf(dir_event, sizeof(dir_event), "%s/%s/device/", SYS_INPUT, js_name);

    n_ev = scandir(dir_event, &namelist_ev, is_event_in_device, alphasort);
    if (n_ev >= 0) {
        for (j = 0; j < n_ev; ++j) {
            if (fd_ev == -1) {
                char event[strlen(DEV_INPUT) + sizeof('/') + strlen(namelist_ev[j]->d_name) + 1];
                snprintf(event, sizeof(event), "%s/%s", DEV_INPUT, namelist_ev[j]->d_name);
                fd_ev = open(event, O_RDWR | O_NONBLOCK);
                if (fd_ev < 0 && errno == EACCES) {
                    fd_ev = open(event, O_RDONLY | O_NONBLOCK);
                }
            }
            free(namelist_ev[j]);
        }
        free(namelist_ev);
    }
    return fd_ev;
}

static unsigned short get_evdev_button_count(int fd) {
    unsigned long key_bits[32];
    memset(key_bits, 0, sizeof(key_bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return 0;
    }
    unsigned short count = 0;
    for (unsigned int i = BTN_JOYSTICK; i <= BTN_JOYSTICK + 0xff && i < BITS_PER_LONG * 32; ++i) {
        if (test_bit(i, key_bits)) {
            count++;
        }
    }
    return count;
}

static int evdev_has_abs(int fd) {
    unsigned long ev_bits[4];
    memset(ev_bits, 0, sizeof(ev_bits));
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
        return 0;
    }
    return test_bit(EV_ABS, ev_bits) ? 1 : 0;
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

static int add_joystick_device(const GPOLL_INTERFACE * poll_interface, int fd_ev, const char * name) {

    if (j_num >= (int)(sizeof(indexToJoystick) / sizeof(*indexToJoystick))) {
        return -1;
    }
    struct joystick_device * device = calloc(1, sizeof(*device));
    if (device == NULL) {
        return -1;
    }
    device->id = j_num;
    indexToJoystick[j_num] = device;
    device->name = strdup(name);
    device->isSixaxis = isSixaxis(name);
    device->fd = fd_ev;
    device->force_feedback.fd = -1;
    device->hat_info.button_nb = get_evdev_button_count(fd_ev);
    device->hid = get_hid(fd_ev);
    open_haptic(device, fd_ev);
    GPOLL_CALLBACKS callbacks = { .fp_read = js_process_events, .fp_write = NULL, .fp_close = js_close_internal };
    poll_interface->fp_register(device->fd, device, &callbacks);
    GLIST_ADD(js_devices, device);
    j_num++;
    return 0;
}

static int js_init(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*)) {

    int ret = 0;
    int i;
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

    /* Phase 1: js-based discovery - resolve event device via sysfs, use evdev for input */
    n_js = scandir(DEV_INPUT, &namelist_js, is_js_device, alphasort);
    if (n_js >= 0) {
        for (i = 0; i < n_js; ++i) {
            int fd_ev = open_evdev(namelist_js[i]->d_name);
            if (fd_ev < 0) {
                free(namelist_js[i]);
                continue;
            }
            if (ioctl(fd_ev, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
                name[0] = '\0';
            }
            if (add_joystick_device(poll_interface, fd_ev, name[0] ? name : namelist_js[i]->d_name) < 0) {
                close(fd_ev);
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

    /* Phase 2: event fallback - add EV_ABS devices not under any js (e.g. event-only gamepads) */
    struct dirent **namelist_ev;
    int n_ev = scandir(SYS_INPUT, &namelist_ev, is_event_device, alphasort);
    if (n_ev >= 0) {
        for (i = 0; i < n_ev; ++i) {
            if (is_event_under_js(namelist_ev[i]->d_name)) {
                free(namelist_ev[i]);
                continue;
            }
            char event_path[strlen(DEV_INPUT) + sizeof('/') + strlen(namelist_ev[i]->d_name) + 1];
            snprintf(event_path, sizeof(event_path), "%s/%s", DEV_INPUT, namelist_ev[i]->d_name);
            int fd_ev = open(event_path, O_RDWR | O_NONBLOCK);
            if (fd_ev < 0 && errno == EACCES) {
                fd_ev = open(event_path, O_RDONLY | O_NONBLOCK);
            }
            if (fd_ev < 0) {
                free(namelist_ev[i]);
                continue;
            }
            if (!evdev_has_abs(fd_ev)) {
                close(fd_ev);
                free(namelist_ev[i]);
                continue;
            }
            if (ioctl(fd_ev, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
                name[0] = '\0';
            }
            if (add_joystick_device(poll_interface, fd_ev, name[0] ? name : namelist_ev[i]->d_name) < 0) {
                close(fd_ev);
            }
            free(namelist_ev[i]);
        }
        free(namelist_ev);
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

    int fd_main = device->fd;
    int fd_ff = device->force_feedback.fd;
    if (fd_main >= 0) {
        fp_remove(fd_main);
        close(fd_main);
        device->fd = -1;
    }
    if (fd_ff >= 0 && fd_ff != fd_main) {
        close(fd_ff);
    }
    device->force_feedback.fd = -1;

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
