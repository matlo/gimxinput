/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef HIDINPUT_H_
#define HIDINPUT_H_

#include <ginput.h>
#include <gimxhid/include/ghid.h>
#include <gimxpoll/include/gpoll.h>

struct hidinput_device_internal;

typedef struct {
    unsigned short vendor_id;
    unsigned short product_id;
    int interface_number; // -1 means any interface
} s_hidinput_ids;

typedef struct {
    s_hidinput_ids * ids;
    // Give the event callback to the driver
    int (* init)(int(*callback)(GE_Event*));
    // Open a device.
    // Synchronous transfers are allowed in this function.
    struct hidinput_device_internal * (* open)(const struct ghid_device_info * dev);
    // Get the ghid_device.
    struct ghid_device * (* get_hid_device)(struct hidinput_device_internal * device);
    // Process a report.
    int (* process)(struct hidinput_device_internal * device, const void * report, unsigned int size);
    // Close a device.
    int (* close)(struct hidinput_device_internal * device);
} s_hidinput_driver;

int hidinput_register(s_hidinput_driver * driver);

int hidinput_init(const GPOLL_INTERFACE * poll_interface, int(*callback)(GE_Event*));
int hidinput_poll();
void hidinput_quit();

int hidinput_set_callbacks(void * dev, void * user, int (* write_cb)(void * user, int transfered), int (* close_cb)(void * user));

#endif /* HIDINPUT_H_ */
