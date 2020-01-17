/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "hidinput.h"
#include <gimxpoll/include/gpoll.h>
#include <gimxcommon/include/gerror.h>
#include <gimxcommon/include/glist.h>
#include <gimxlog/include/glog.h>
#include <stdlib.h>
#include <string.h>

GLOG_GET(GLOG_NAME)

static s_hidinput_driver ** drivers = NULL;
static unsigned int nb_drivers = 0;

struct hidinput_device {
    s_hidinput_driver * driver;
    struct hidinput_device_internal * device;
    struct ghid_device * hid;
    int read_pending;
    struct {
        void * user;
        int (* write)(void * user, int transfered);
        int (* close)(void * user);
    } callbacks;
    GLIST_LINK(struct hidinput_device);
};

static int close_device(struct hidinput_device * device) {

    if (device->driver != NULL && device->device != NULL) {
        device->driver->close(device->device);
    }

    GLIST_REMOVE(hidinput_devices, device);

    free(device);

    return 0;
}

static GLIST_INST(struct hidinput_device, hidinput_devices);

static int read_callback(void * user, const void * buf, int status) {

    struct hidinput_device * device = (struct hidinput_device *) user;

    int ret = 0;

    device->read_pending = 0;

    if (status > 0) {
        if (device->driver->process(device->device, buf, status) < 0) {
          ret = -1;
        }
    }

    return ret;
}

static int write_callback(void * user, int status) {

    struct hidinput_device * device = (struct hidinput_device *) user;

    return device->callbacks.write(device->callbacks.user, status);
}

static int close_callback(void * user) {

    struct hidinput_device * device = (struct hidinput_device *) user;

    int ret = 0;

    if (device->callbacks.close != NULL) {
        ret = device->callbacks.close(device->callbacks.user);
    }

    close_device(device);

    return ret;
}

void hidinput_destructor(void) __attribute__((destructor));
void hidinput_destructor(void) {

    free(drivers);
    nb_drivers = 0;
}

int hidinput_register(s_hidinput_driver * driver) {

    void * ptr = realloc(drivers, (nb_drivers + 1) * sizeof(*drivers));
    if (ptr == NULL) {
        PRINT_ERROR_ALLOC_FAILED("realloc");
        return -1;
    }
    drivers = ptr;
    drivers[nb_drivers] = driver;
    ++nb_drivers;
    return 0;
}

int hidinput_init(const GPOLL_INTERFACE * poll_interface, int(*callback)(GE_Event*)) {

    if (callback == NULL) {
      PRINT_ERROR_OTHER("callback is NULL");
      return -1;
    }

    if (poll_interface->fp_register == NULL) {
        PRINT_ERROR_OTHER("fp_register_fd is NULL");
        return -1;
    }

    if (poll_interface->fp_remove == NULL) {
        PRINT_ERROR_OTHER("fp_remove is NULL");
        return -1;
    }

    unsigned int driver;
    for (driver = 0; driver < nb_drivers; ++driver) {
        drivers[driver]->init(callback);
    }

    struct ghid_device_info * hid_devs = ghid_enumerate(0x0000, 0x0000);
    struct ghid_device_info * current;
    for (current = hid_devs; current != NULL; current = current->next) {
        for (driver = 0; driver < nb_drivers; ++driver) {
            unsigned int id;
            for (id = 0; drivers[driver]->ids[id].vendor_id != 0; ++id) {
                if (drivers[driver]->ids[id].vendor_id == current->vendor_id
                        && drivers[driver]->ids[id].product_id == current->product_id
                        && (drivers[driver]->ids[id].interface_number == -1
                                || drivers[driver]->ids[id].interface_number == current->interface_number)) {
                    struct hidinput_device_internal * device_internal = drivers[driver]->open(current);
                    if (device_internal != NULL) {
                        struct hidinput_device * device = calloc(1, sizeof(*device));
                        if (device != NULL) {
                            device->driver = drivers[driver];
                            device->device = device_internal;
                            device->hid = drivers[driver]->get_hid_device(device_internal);
                            GHID_CALLBACKS callbacks = {
                                    .fp_read = read_callback,
                                    .fp_write = write_callback,
                                    .fp_close = close_callback,
                                    .fp_register = poll_interface->fp_register,
                                    .fp_remove = poll_interface->fp_remove,
                            };
                            if (ghid_register(device->hid, device, &callbacks) < 0) {
                                close_device(device);
                                free(device);
                            } else {
                                GLIST_ADD(hidinput_devices, device);
                            }
                        } else {
                            PRINT_ERROR_ALLOC_FAILED("calloc");
                            drivers[driver]->close(device_internal);
                        }
                    }
                }
            }
        }
    }
    ghid_free_enumeration(hid_devs);

    return 0;
}

int hidinput_poll() {

    int ret = 0;
    struct hidinput_device * device;
    for (device = GLIST_BEGIN(hidinput_devices); device != GLIST_END(hidinput_devices); device = device->next) {
        if (device->read_pending == 0) {
            if (ghid_poll(device->hid) < 0) {
                ret = -1;
            } else {
                device->read_pending = 1;
            }
        }
    }
    return ret;
}

void hidinput_quit() {

    GLIST_CLEAN_ALL(hidinput_devices, close_device)
}

int hidinput_set_callbacks(void * dev, void * user, int (* write_cb)(void * user, int transfered), int (* close_cb)(void * user)) {

    // to be safe, check this device exists

    struct hidinput_device * device;
    for (device = GLIST_BEGIN(hidinput_devices); device != GLIST_END(hidinput_devices); device = device->next) {
        if (dev == device->hid) {
            device->callbacks.user = user;
            device->callbacks.write = write_cb;
            device->callbacks.close = close_cb;
            return 0;
        }
    }

    return -1;
}
