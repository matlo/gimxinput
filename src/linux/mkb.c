/*
 Copyright (c) 2013 Mathieu Laurendeau
 License: GPLv3
 */

#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
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

#define MAX_KEYNAMES (KEY_MICMUTE+1)

#define DEVTYPE_KEYBOARD 0x01
#define DEVTYPE_MOUSE    0x02
#define DEVTYPE_NB       2

static GPOLL_REMOVE_SOURCE fp_remove = NULL;

struct mkb_device
{
  int fd;
  int mouse;
  int keyboard;
  char* name;
  GLIST_LINK(struct mkb_device);
};

static int k_num;
static int m_num;

static int grab = 0;

static int mkb_close_device(void * user) {

    struct mkb_device * device = (struct mkb_device *) user;

    free(device->name);

    if (device->fd >= 0) {
        if (grab) {
            ioctl(device->fd, EVIOCGRAB, (void *) 0);
        }
        fp_remove(device->fd);
        close(device->fd);
    }

    GLIST_REMOVE(mkb_devices, device);

    free(device);

    return 0;
}

static GLIST_INST(struct mkb_device, mkb_devices);

#define LONG_BITS (sizeof(long) * 8)
#define NLONGS(x) (((x) + LONG_BITS - 1) / LONG_BITS)

static inline int BitIsSet(const unsigned long *array, int bit) {
    return !!(array[bit / LONG_BITS] & (1LL << (bit % LONG_BITS)));
}

static int mkb_read_type(struct mkb_device * device, int fd) {

    char name[1024] = { 0 };
    unsigned long key_bitmask[NLONGS(KEY_CNT)] = { 0 };
    unsigned long rel_bitmask[NLONGS(REL_CNT)] = { 0 };
    int i, len;
    int has_rel_axes = 0;
    int has_keys = 0;
    int has_scroll = 0;

    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
        PRINT_ERROR_ERRNO("ioctl EVIOCGNAME");
        return -1;
    }

    len = ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bitmask)), rel_bitmask);
    if (len < 0) {
        PRINT_ERROR_ERRNO("ioctl EVIOCGBIT");
        return -1;
    }

    len = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bitmask)), key_bitmask);
    if (len < 0) {
        PRINT_ERROR_ERRNO("ioctl EVIOCGBIT");
        return -1;
    }

    for (i = 0; i < REL_MAX; i++) {
        if (BitIsSet(rel_bitmask, i)) {
            has_rel_axes = 1;
            break;
        }
    }

    if (has_rel_axes) {
        if (BitIsSet(rel_bitmask, REL_WHEEL) || BitIsSet(rel_bitmask, REL_HWHEEL) || BitIsSet(rel_bitmask, REL_DIAL)) {
            has_scroll = 1;
        }
    }

    for (i = 0; i < BTN_MISC; i++) {
        if (BitIsSet(key_bitmask, i)) {
            has_keys = 1;
            break;
        }
    }

    if (!has_rel_axes && !has_keys && !has_scroll) {
        return -1;
    }

    device->name = strdup(name);
    if (device->name == NULL) {
        PRINT_ERROR_ERRNO("strdup");
        return -1;
    }

    if (has_keys) {
        device->keyboard = k_num;
        k_num++;
    }
    if (has_rel_axes || has_scroll) {
        device->mouse = m_num;
        m_num++;
    }

    return 0;
}

static int (*event_callback)(GE_Event*) = NULL;

static void mkb_process_event(struct mkb_device * device, struct input_event* ie) {

    GE_Event evt = { };

    switch (ie->type) {
    case EV_KEY:
        if (ie->value > 1) {
            return;
        }
        break;
    case EV_MSC:
        if (ie->value > 1) {
            return;
        }
        if (ie->value == 2) {
            return;
        }
        break;
    case EV_REL:
    case EV_ABS:
        break;
    default:
        return;
    }
    if (device->keyboard >= 0) {
        if (ie->type == EV_KEY) {
            if (ie->code > 0 && ie->code < MAX_KEYNAMES) {
                evt.type = ie->value ? GE_KEYDOWN : GE_KEYUP;
                evt.key.which = device->keyboard;
                evt.key.keysym = ie->code;
            }
        }
    }
    if (device->mouse >= 0) {
        if (ie->type == EV_KEY) {
            if (ie->code >= BTN_LEFT && ie->code <= BTN_TASK) {
                evt.type = ie->value ? GE_MOUSEBUTTONDOWN : GE_MOUSEBUTTONUP;
                evt.button.which = device->mouse;
                evt.button.button = ie->code - BTN_MOUSE;
            }
        } else if (ie->type == EV_REL) {
            if (ie->code == REL_X) {
                evt.type = GE_MOUSEMOTION;
                evt.motion.which = device->mouse;
                evt.motion.xrel = ie->value;
            } else if (ie->code == REL_Y) {
                evt.type = GE_MOUSEMOTION;
                evt.motion.which = device->mouse;
                evt.motion.yrel = ie->value;
            } else if (ie->code == REL_WHEEL) {
                evt.type = GE_MOUSEBUTTONDOWN;
                evt.button.which = device->mouse;
                evt.button.button = (ie->value > 0) ? GE_BTN_WHEELUP : GE_BTN_WHEELDOWN;
            } else if (ie->code == REL_HWHEEL) {
                evt.type = GE_MOUSEBUTTONDOWN;
                evt.button.which = device->mouse;
                evt.button.button = (ie->value > 0) ? GE_BTN_WHEELRIGHT : GE_BTN_WHEELLEFT;
            }
        }
    }

    /*
     * Process evt.
     */
    if (evt.type != GE_NOEVENT) {
        eprintf("event from device: %s\n", device->name);
        eprintf("type: %d code: %d value: %d\n", ie->type, ie->code, ie->value);
        event_callback(&evt);
        if (evt.type == GE_MOUSEBUTTONDOWN) {
            if (ie->code == REL_WHEEL || ie->code == REL_HWHEEL) {
                evt.type = GE_MOUSEBUTTONUP;
                event_callback(&evt);
            }
        }
    }
}

static int mkb_process_events(void * user) {

    struct mkb_device * device = (struct mkb_device *) user;

    static struct input_event ie[MAX_EVENTS];

    int res = read(device->fd, ie, sizeof(ie));
    if (res > 0) {
        unsigned int j;
        for (j = 0; j < res / sizeof(*ie); ++j) {
            mkb_process_event(device, ie + j);
        }
    } else if (res < 0 && errno != EAGAIN) {
        mkb_close_device(device);
    }
    return 0;
}

#define DEV_INPUT "/dev/input"
#define EV_DEV_NAME "event%u"

static int is_event_file(const struct dirent *dir) {

    unsigned int num;
    if (dir->d_type == DT_CHR && sscanf(dir->d_name, EV_DEV_NAME, &num) == 1 && num < 256) {
        return 1;
    }
    return 0;
}

static int mkb_init(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*)) {

    int ret = 0;
    int i;
    int fd;

    if (poll_interface->fp_register == NULL) {
        PRINT_ERROR_OTHER("fp_register is NULL");
        return -1;
    }

    if (poll_interface->fp_remove == NULL) {
        PRINT_ERROR_OTHER("fp_remove is NULL");
        return -1;
    }

    k_num = 0;
    m_num = 0;

    if (callback == NULL) {
        PRINT_ERROR_OTHER("callback is NULL");
        return -1;
    }

    event_callback = callback;
    fp_remove = poll_interface->fp_remove;

    struct dirent **namelist;
    int n;

    n = scandir(DEV_INPUT, &namelist, is_event_file, alphasort);
    if (n >= 0) {
        for (i = 0; i < n; ++i) {
        	char device[strlen(DEV_INPUT) + sizeof('/') + strlen(namelist[i]->d_name) + 1];
        	snprintf(device, sizeof(device), "%s/%s", DEV_INPUT, namelist[i]->d_name);

            fd = open(device, O_RDONLY | O_NONBLOCK);
            if (fd != -1) {
                struct mkb_device * device = calloc(1, sizeof(*device));
                if (device != NULL) {
                    device->mouse = -1;
                    device->keyboard = -1;
                    if (mkb_read_type(device, fd) != -1) {
                        device->fd = fd;
                        if (grab) {
                            ioctl(device->fd, EVIOCGRAB, (void *) 1);
                        }
                        GPOLL_CALLBACKS callbacks = { .fp_read = mkb_process_events, .fp_write = NULL, .fp_close =
                                mkb_close_device };
                        poll_interface->fp_register(device->fd, device, &callbacks);
                        GLIST_ADD(mkb_devices, device);
                    } else {
                        close(fd);
                        free(device);
                    }
                } else {
                    PRINT_ERROR_ALLOC_FAILED("calloc");
                    close(fd);
                }
            } else {
                if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
                    fprintf(stderr, "%s:%d %s: opening %s failed with error: %m\n", __FILE__, __LINE__, __func__, device);
                }
            }

            free(namelist[i]);
        }
        free(namelist);
    } else {
        PRINT_ERROR_ERRNO("scandir");
        ret = -1;
    }

    return ret;
}

static char* mkb_get_name(unsigned char devtype, int index) {

    struct mkb_device * device = GLIST_BEGIN(mkb_devices);
    while (device != GLIST_END(mkb_devices)) {
        switch(devtype) {
        case DEVTYPE_MOUSE:
            if (device->mouse == index) {
                return device->name;
            }
            break;
        case DEVTYPE_KEYBOARD:
            if (device->keyboard == index) {
                return device->name;
            }
            break;
        }
        device = device->next;
    }
    return NULL;
}

static const char * mkb_get_keyboard_name(int index) {

    return mkb_get_name(DEVTYPE_KEYBOARD, index);
}

static const char * mkb_get_mouse_name(int index) {

    return mkb_get_name(DEVTYPE_MOUSE, index);
}

static void mkb_quit() {

    GLIST_CLEAN_ALL(mkb_devices, mkb_close_device)

    if (!grab) {
        tcflush(STDIN_FILENO, TCIFLUSH);
    }
}

static int mkb_grab(int mode) {

    int one = 1;
    int* enable = NULL;
    if (mode == GE_GRAB_ON) {
        enable = &one;
    }
    struct mkb_device * device = GLIST_BEGIN(mkb_devices);
    while (device != GLIST_END(mkb_devices)) {
        ioctl(device->fd, EVIOCGRAB, enable);
        device = device->next;
    }

    return mode;
}

static int mkb_get_src() {

    return GE_MKB_SOURCE_PHYSICAL;
}

static struct mkb_source source = {
    .init = mkb_init,
    .get_src = mkb_get_src,
    .grab = mkb_grab,
    .get_mouse_name = mkb_get_mouse_name,
    .get_keyboard_name = mkb_get_keyboard_name,
    .sync_process = NULL,
    .quit = mkb_quit,
};

void mkb_constructor() __attribute__((constructor));
void mkb_constructor() {
    ev_register_mkb_source(&source);
}
