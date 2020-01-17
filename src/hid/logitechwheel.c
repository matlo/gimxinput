/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "hidinput.h"
#ifdef UHID
#include <gimxuhid/include/guhid.h>
#endif
#include <gimxcommon/include/gerror.h>
#include <gimxcommon/include/glist.h>
#include <gimxlog/include/glog.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef WIN32
#include <windows.h>
#endif

#define USB_VENDOR_ID_LOGITECH                  0x046d

#define USB_PRODUCT_ID_LOGITECH_FORMULA_YELLOW   0xc202 // no force feedback
#define USB_PRODUCT_ID_LOGITECH_FORMULA_GP       0xc20e // no force feedback
#define USB_PRODUCT_ID_LOGITECH_FORMULA_FORCE    0xc291 // i-force protocol
#define USB_PRODUCT_ID_LOGITECH_FORMULA_FORCE_GP 0xc293 // classic protocol
#define USB_PRODUCT_ID_LOGITECH_DRIVING_FORCE    0xc294 // classic protocol
#define USB_PRODUCT_ID_LOGITECH_MOMO_WHEEL       0xc295 // classic protocol
#define USB_PRODUCT_ID_LOGITECH_DFP_WHEEL        0xc298 // classic protocol
#define USB_PRODUCT_ID_LOGITECH_G25_WHEEL        0xc299 // classic protocol
#define USB_PRODUCT_ID_LOGITECH_DFGT_WHEEL       0xc29a // classic protocol
#define USB_PRODUCT_ID_LOGITECH_G27_WHEEL        0xc29b // classic protocol
#define USB_PRODUCT_ID_LOGITECH_WII_WHEEL        0xc29c // rumble only
#define USB_PRODUCT_ID_LOGITECH_MOMO_WHEEL2      0xca03 // classic protocol
#define USB_PRODUCT_ID_LOGITECH_VIBRATION_WHEEL  0xca04 // rumble only
#define USB_PRODUCT_ID_LOGITECH_G920_XONE_WHEEL  0xc261 // Xbox One protocol
#define USB_PRODUCT_ID_LOGITECH_G920_WHEEL       0xc262 // hid++ protocol only
#define USB_PRODUCT_ID_LOGITECH_G29_PC_WHEEL     0xc24f // classic protocol
#define USB_PRODUCT_ID_LOGITECH_G29_PS4_WHEEL    0xc260 // classic protocol with 1 byte offset

#define FF_LG_OUTPUT_REPORT_SIZE 7

GLOG_GET(GLOG_NAME)

struct hidinput_device_internal {
    struct ghid_device * hid;
#ifdef UHID
    struct guhid_device * uhid;
#endif
    GLIST_LINK(struct hidinput_device_internal);
};

static int close_device(struct hidinput_device_internal * device) {

#ifdef UHID
    if (device->uhid != NULL) {
        guhid_close(device->uhid);
    }
    if (device->hid != NULL) {
        ghid_close(device->hid);
    }
#endif

    GLIST_REMOVE(lgw_devices, device);

    free(device);

    return 0;
}

static GLIST_INST(struct hidinput_device_internal, lgw_devices);

#define MAKE_IDS(USB_PRODUCT_ID) \
    { .vendor_id = USB_VENDOR_ID_LOGITECH, .product_id = USB_PRODUCT_ID, .interface_number = -1 }

static s_hidinput_ids ids[] = {
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_FORMULA_FORCE),
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_FORMULA_FORCE_GP),
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_DRIVING_FORCE),
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_MOMO_WHEEL),
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_DFP_WHEEL),
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_G25_WHEEL),
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_DFGT_WHEEL),
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_G27_WHEEL),
        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_MOMO_WHEEL2),
// handle the G29 through OS translation (there is some issue on Windows)
//        MAKE_IDS(USB_PRODUCT_ID_LOGITECH_G29_PC_WHEEL),
        { .vendor_id = 0, .product_id = 0 },
};

static int init(int(*callback)(GE_Event*) __attribute__((unused))) {

    return 0;
}

#ifdef UHID
static int process(struct hidinput_device_internal * device, const void * report, unsigned int size) {

    int ret = guhid_write(device->uhid, report, size);

    return ret < 0 ? -1 : 0;
}
#else
static int process(struct hidinput_device_internal * device __attribute__((unused)), const void * report __attribute__((unused)),
    unsigned int size __attribute__((unused))) {

    return 0;
}
#endif

#ifdef UHID
/* Fixed report descriptors for Logitech Driving Force (and Pro)
 * wheel controllers
 *
 * The original descriptors hide the separate throttle and brake axes in
 * a custom vendor usage page, providing only a combined value as
 * GenericDesktop.Y.
 * These descriptors remove the combined Y axis and instead report
 * separate throttle (Y) and brake (RZ).
 */
static __u8 df_rdesc_fixed[] = {
        0x05, 0x01, /*  Usage Page (Desktop),                   */
        0x09, 0x04, /*  Usage (Joystik),                        */
        0xA1, 0x01, /*  Collection (Application),               */
        0xA1, 0x02, /*      Collection (Logical),               */
        0x95, 0x01, /*          Report Count (1),               */
        0x75, 0x0A, /*          Report Size (10),               */
        0x14, /*          Logical Minimum (0),            */
        0x26, 0xFF, 0x03, /*          Logical Maximum (1023),         */
        0x34, /*          Physical Minimum (0),           */
        0x46, 0xFF, 0x03, /*          Physical Maximum (1023),        */
        0x09, 0x30, /*          Usage (X),                      */
        0x81, 0x02, /*          Input (Variable),               */
        0x95, 0x0C, /*          Report Count (12),              */
        0x75, 0x01, /*          Report Size (1),                */
        0x25, 0x01, /*          Logical Maximum (1),            */
        0x45, 0x01, /*          Physical Maximum (1),           */
        0x05, 0x09, /*          Usage (Buttons),                */
        0x19, 0x01, /*          Usage Minimum (1),              */
        0x29, 0x0c, /*          Usage Maximum (12),             */
        0x81, 0x02, /*          Input (Variable),               */
        0x95, 0x02, /*          Report Count (2),               */
        0x06, 0x00, 0xFF, /*          Usage Page (Vendor: 65280),     */
        0x09, 0x01, /*          Usage (?: 1),                   */
        0x81, 0x02, /*          Input (Variable),               */
        0x05, 0x01, /*          Usage Page (Desktop),           */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),          */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),         */
        0x95, 0x01, /*          Report Count (1),               */
        0x75, 0x08, /*          Report Size (8),                */
        0x81, 0x02, /*          Input (Variable),               */
        0x25, 0x07, /*          Logical Maximum (7),            */
        0x46, 0x3B, 0x01, /*          Physical Maximum (315),         */
        0x75, 0x04, /*          Report Size (4),                */
        0x65, 0x14, /*          Unit (Degrees),                 */
        0x09, 0x39, /*          Usage (Hat Switch),             */
        0x81, 0x42, /*          Input (Variable, Null State),   */
        0x75, 0x01, /*          Report Size (1),                */
        0x95, 0x04, /*          Report Count (4),               */
        0x65, 0x00, /*          Unit (none),                    */
        0x06, 0x00, 0xFF, /*          Usage Page (Vendor: 65280),     */
        0x09, 0x01, /*          Usage (?: 1),                   */
        0x25, 0x01, /*          Logical Maximum (1),            */
        0x45, 0x01, /*          Physical Maximum (1),           */
        0x81, 0x02, /*          Input (Variable),               */
        0x05, 0x01, /*          Usage Page (Desktop),           */
        0x95, 0x01, /*          Report Count (1),               */
        0x75, 0x08, /*          Report Size (8),                */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),          */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),         */
        0x09, 0x31, /*          Usage (Y),                      */
        0x81, 0x02, /*          Input (Variable),               */
        0x09, 0x35, /*          Usage (Rz),                     */
        0x81, 0x02, /*          Input (Variable),               */
        0xC0, /*      End Collection,                     */
        0xA1, 0x02, /*      Collection (Logical),               */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),          */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),         */
        0x95, 0x07, /*          Report Count (7),               */
        0x75, 0x08, /*          Report Size (8),                */
        0x09, 0x03, /*          Usage (?: 3),                   */
        0x91, 0x02, /*          Output (Variable),              */
        0xC0, /*      End Collection,                     */
        0xC0 /*  End Collection                          */
};

static __u8 dfp_rdesc_fixed[] = {
        0x05, 0x01, /*  Usage Page (Desktop),                   */
        0x09, 0x04, /*  Usage (Joystik),                        */
        0xA1, 0x01, /*  Collection (Application),               */
        0xA1, 0x02, /*      Collection (Logical),               */
        0x95, 0x01, /*          Report Count (1),               */
        0x75, 0x0E, /*          Report Size (14),               */
        0x14, /*          Logical Minimum (0),            */
        0x26, 0xFF, 0x3F, /*          Logical Maximum (16383),        */
        0x34, /*          Physical Minimum (0),           */
        0x46, 0xFF, 0x3F, /*          Physical Maximum (16383),       */
        0x09, 0x30, /*          Usage (X),                      */
        0x81, 0x02, /*          Input (Variable),               */
        0x95, 0x0E, /*          Report Count (14),              */
        0x75, 0x01, /*          Report Size (1),                */
        0x25, 0x01, /*          Logical Maximum (1),            */
        0x45, 0x01, /*          Physical Maximum (1),           */
        0x05, 0x09, /*          Usage Page (Button),            */
        0x19, 0x01, /*          Usage Minimum (01h),            */
        0x29, 0x0E, /*          Usage Maximum (0Eh),            */
        0x81, 0x02, /*          Input (Variable),               */
        0x05, 0x01, /*          Usage Page (Desktop),           */
        0x95, 0x01, /*          Report Count (1),               */
        0x75, 0x04, /*          Report Size (4),                */
        0x25, 0x07, /*          Logical Maximum (7),            */
        0x46, 0x3B, 0x01, /*          Physical Maximum (315),         */
        0x65, 0x14, /*          Unit (Degrees),                 */
        0x09, 0x39, /*          Usage (Hat Switch),             */
        0x81, 0x42, /*          Input (Variable, Nullstate),    */
        0x65, 0x00, /*          Unit,                           */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),          */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),         */
        0x75, 0x08, /*          Report Size (8),                */
        0x81, 0x01, /*          Input (Constant),               */
        0x09, 0x31, /*          Usage (Y),                      */
        0x81, 0x02, /*          Input (Variable),               */
        0x09, 0x35, /*          Usage (Rz),                     */
        0x81, 0x02, /*          Input (Variable),               */
        0x81, 0x01, /*          Input (Constant),               */
        0xC0, /*      End Collection,                     */
        0xA1, 0x02, /*      Collection (Logical),               */
        0x09, 0x02, /*          Usage (02h),                    */
        0x95, 0x07, /*          Report Count (7),               */
        0x91, 0x02, /*          Output (Variable),              */
        0xC0, /*      End Collection,                     */
        0xC0 /*  End Collection                          */
};

static __u8 fv_rdesc_fixed[] = {
        0x05, 0x01, /*  Usage Page (Desktop),                   */
        0x09, 0x04, /*  Usage (Joystik),                        */
        0xA1, 0x01, /*  Collection (Application),               */
        0xA1, 0x02, /*      Collection (Logical),               */
        0x95, 0x01, /*          Report Count (1),               */
        0x75, 0x0A, /*          Report Size (10),               */
        0x15, 0x00, /*          Logical Minimum (0),            */
        0x26, 0xFF, 0x03, /*          Logical Maximum (1023),         */
        0x35, 0x00, /*          Physical Minimum (0),           */
        0x46, 0xFF, 0x03, /*          Physical Maximum (1023),        */
        0x09, 0x30, /*          Usage (X),                      */
        0x81, 0x02, /*          Input (Variable),               */
        0x95, 0x0C, /*          Report Count (12),              */
        0x75, 0x01, /*          Report Size (1),                */
        0x25, 0x01, /*          Logical Maximum (1),            */
        0x45, 0x01, /*          Physical Maximum (1),           */
        0x05, 0x09, /*          Usage Page (Button),            */
        0x19, 0x01, /*          Usage Minimum (01h),            */
        0x29, 0x0C, /*          Usage Maximum (0Ch),            */
        0x81, 0x02, /*          Input (Variable),               */
        0x95, 0x02, /*          Report Count (2),               */
        0x06, 0x00, 0xFF, /*          Usage Page (FF00h),             */
        0x09, 0x01, /*          Usage (01h),                    */
        0x81, 0x02, /*          Input (Variable),               */
        0x09, 0x02, /*          Usage (02h),                    */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),          */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),         */
        0x95, 0x01, /*          Report Count (1),               */
        0x75, 0x08, /*          Report Size (8),                */
        0x81, 0x02, /*          Input (Variable),               */
        0x05, 0x01, /*          Usage Page (Desktop),           */
        0x25, 0x07, /*          Logical Maximum (7),            */
        0x46, 0x3B, 0x01, /*          Physical Maximum (315),         */
        0x75, 0x04, /*          Report Size (4),                */
        0x65, 0x14, /*          Unit (Degrees),                 */
        0x09, 0x39, /*          Usage (Hat Switch),             */
        0x81, 0x42, /*          Input (Variable, Null State),   */
        0x75, 0x01, /*          Report Size (1),                */
        0x95, 0x04, /*          Report Count (4),               */
        0x65, 0x00, /*          Unit,                           */
        0x06, 0x00, 0xFF, /*          Usage Page (FF00h),             */
        0x09, 0x01, /*          Usage (01h),                    */
        0x25, 0x01, /*          Logical Maximum (1),            */
        0x45, 0x01, /*          Physical Maximum (1),           */
        0x81, 0x02, /*          Input (Variable),               */
        0x05, 0x01, /*          Usage Page (Desktop),           */
        0x95, 0x01, /*          Report Count (1),               */
        0x75, 0x08, /*          Report Size (8),                */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),          */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),         */
        0x09, 0x31, /*          Usage (Y),                      */
        0x81, 0x02, /*          Input (Variable),               */
        0x09, 0x32, /*          Usage (Z),                      */
        0x81, 0x02, /*          Input (Variable),               */
        0xC0, /*      End Collection,                     */
        0xA1, 0x02, /*      Collection (Logical),               */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),          */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),         */
        0x95, 0x07, /*          Report Count (7),               */
        0x75, 0x08, /*          Report Size (8),                */
        0x09, 0x03, /*          Usage (03h),                    */
        0x91, 0x02, /*          Output (Variable),              */
        0xC0, /*      End Collection,                     */
        0xC0 /*  End Collection                          */
};

static __u8 momo_rdesc_fixed[] = {
        0x05, 0x01, /*  Usage Page (Desktop),               */
        0x09, 0x04, /*  Usage (Joystik),                    */
        0xA1, 0x01, /*  Collection (Application),           */
        0xA1, 0x02, /*      Collection (Logical),           */
        0x95, 0x01, /*          Report Count (1),           */
        0x75, 0x0A, /*          Report Size (10),           */
        0x15, 0x00, /*          Logical Minimum (0),        */
        0x26, 0xFF, 0x03, /*          Logical Maximum (1023),     */
        0x35, 0x00, /*          Physical Minimum (0),       */
        0x46, 0xFF, 0x03, /*          Physical Maximum (1023),    */
        0x09, 0x30, /*          Usage (X),                  */
        0x81, 0x02, /*          Input (Variable),           */
        0x95, 0x08, /*          Report Count (8),           */
        0x75, 0x01, /*          Report Size (1),            */
        0x25, 0x01, /*          Logical Maximum (1),        */
        0x45, 0x01, /*          Physical Maximum (1),       */
        0x05, 0x09, /*          Usage Page (Button),        */
        0x19, 0x01, /*          Usage Minimum (01h),        */
        0x29, 0x08, /*          Usage Maximum (08h),        */
        0x81, 0x02, /*          Input (Variable),           */
        0x06, 0x00, 0xFF, /*          Usage Page (FF00h),         */
        0x75, 0x0E, /*          Report Size (14),           */
        0x95, 0x01, /*          Report Count (1),           */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),      */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),     */
        0x09, 0x00, /*          Usage (00h),                */
        0x81, 0x02, /*          Input (Variable),           */
        0x05, 0x01, /*          Usage Page (Desktop),       */
        0x75, 0x08, /*          Report Size (8),            */
        0x09, 0x31, /*          Usage (Y),                  */
        0x81, 0x02, /*          Input (Variable),           */
        0x09, 0x32, /*          Usage (Z),                  */
        0x81, 0x02, /*          Input (Variable),           */
        0x06, 0x00, 0xFF, /*          Usage Page (FF00h),         */
        0x09, 0x01, /*          Usage (01h),                */
        0x81, 0x02, /*          Input (Variable),           */
        0xC0, /*      End Collection,                 */
        0xA1, 0x02, /*      Collection (Logical),           */
        0x09, 0x02, /*          Usage (02h),                */
        0x95, 0x07, /*          Report Count (7),           */
        0x91, 0x02, /*          Output (Variable),          */
        0xC0, /*      End Collection,                 */
        0xC0 /*  End Collection                      */
};

static __u8 ffgp_rdesc_fixed[] = {
        0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
        0x09, 0x04,        // Usage (Joystick)
        0xA1, 0x01,        // Collection (Application)
        0xA1, 0x02,        //   Collection (Logical)
        0x95, 0x01,        //     Report Count (1)
        0x75, 0x0A,        //     Report Size (10)
        0x15, 0x00,        //     Logical Minimum (0)
        0x26, 0xFF, 0x03,  //     Logical Maximum (1023)
        0x35, 0x00,        //     Physical Minimum (0)
        0x46, 0xFF, 0x03,  //     Physical Maximum (1023)
        0x09, 0x30,        //     Usage (X)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x95, 0x06,        //     Report Count (6)
        0x75, 0x01,        //     Report Size (1)
        0x25, 0x01,        //     Logical Maximum (1)
        0x45, 0x01,        //     Physical Maximum (1)
        0x05, 0x09,        //     Usage Page (Button)
        0x19, 0x01,        //     Usage Minimum (0x01)
        0x29, 0x06,        //     Usage Maximum (0x06)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x95, 0x01,        //     Report Count (1)
        0x75, 0x08,        //     Report Size (8)
        0x26, 0xFF, 0x00,  //     Logical Maximum (255)
        0x46, 0xFF, 0x00,  //     Physical Maximum (255)
        //0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
        0x09, 0x01,        //     Usage (0x01)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
        0x09, 0x31,        //     Usage (Y)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        //0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
        0x09, 0x01,        //     Usage (0x01)
        0x95, 0x03,        //     Report Count (3)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0xC0,              //   End Collection
        0xA1, 0x02,        //   Collection (Logical)
        0x09, 0x02,        //     Usage (0x02)
        0x95, 0x07,        //     Report Count (7)
        0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0xC0,              //   End Collection
        0xC0,              // End Collection
};

static __u8 momo2_rdesc_fixed[] = {
        0x05, 0x01, /*  Usage Page (Desktop),               */
        0x09, 0x04, /*  Usage (Joystik),                    */
        0xA1, 0x01, /*  Collection (Application),           */
        0xA1, 0x02, /*      Collection (Logical),           */
        0x95, 0x01, /*          Report Count (1),           */
        0x75, 0x0A, /*          Report Size (10),           */
        0x15, 0x00, /*          Logical Minimum (0),        */
        0x26, 0xFF, 0x03, /*          Logical Maximum (1023),     */
        0x35, 0x00, /*          Physical Minimum (0),       */
        0x46, 0xFF, 0x03, /*          Physical Maximum (1023),    */
        0x09, 0x30, /*          Usage (X),                  */
        0x81, 0x02, /*          Input (Variable),           */
        0x95, 0x0A, /*          Report Count (10),          */
        0x75, 0x01, /*          Report Size (1),            */
        0x25, 0x01, /*          Logical Maximum (1),        */
        0x45, 0x01, /*          Physical Maximum (1),       */
        0x05, 0x09, /*          Usage Page (Button),        */
        0x19, 0x01, /*          Usage Minimum (01h),        */
        0x29, 0x0A, /*          Usage Maximum (0Ah),        */
        0x81, 0x02, /*          Input (Variable),           */
        0x06, 0x00, 0xFF, /*          Usage Page (FF00h),         */
        0x09, 0x00, /*          Usage (00h),                */
        0x95, 0x04, /*          Report Count (4),           */
        0x81, 0x02, /*          Input (Variable),           */
        0x95, 0x01, /*          Report Count (1),           */
        0x75, 0x08, /*          Report Size (8),            */
        0x26, 0xFF, 0x00, /*          Logical Maximum (255),      */
        0x46, 0xFF, 0x00, /*          Physical Maximum (255),     */
        0x09, 0x01, /*          Usage (01h),                */
        0x81, 0x02, /*          Input (Variable),           */
        0x05, 0x01, /*          Usage Page (Desktop),       */
        0x09, 0x31, /*          Usage (Y),                  */
        0x81, 0x02, /*          Input (Variable),           */
        0x09, 0x32, /*          Usage (Z),                  */
        0x81, 0x02, /*          Input (Variable),           */
        0x06, 0x00, 0xFF, /*          Usage Page (FF00h),         */
        0x09, 0x00, /*          Usage (00h),                */
        0x81, 0x02, /*          Input (Variable),           */
        0xC0, /*      End Collection,                 */
        0xA1, 0x02, /*      Collection (Logical),           */
        0x09, 0x02, /*          Usage (02h),                */
        0x95, 0x07, /*          Report Count (7),           */
        0x91, 0x02, /*          Output (Variable),          */
        0xC0, /*      End Collection,                 */
        0xC0 /*  End Collection                      */
};

/*
 * See http://wiibrew.org/wiki/Logitech_USB_steering_wheel
 */
static __u8 wii_rdesc_fixed[] = {
        0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
        0x09, 0x04,        // Usage (Joystick)
        0xA1, 0x01,        // Collection (Application)
        0xA1, 0x02,        //   Collection (Logical)
        0x95, 0x01,        //     Report Count (1)
        0x75, 0x0A,        //     Report Size (10)
        0x15, 0x00,        //     Logical Minimum (0)
        0x26, 0xFF, 0x03,  //     Logical Maximum (1023)
        0x35, 0x00,        //     Physical Minimum (0)
        0x46, 0xFF, 0x03,  //     Physical Maximum (1023)
        0x09, 0x30,        //     Usage (X)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
        0x95, 0x02,        //     Report Count (2)
        0x75, 0x01,        //     Report Size (1)
        0x25, 0x01,        //     Logical Maximum (1)
        0x45, 0x01,        //     Physical Maximum (1)
        0x09, 0x01,        //     Usage (0x01)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x05, 0x09,        //     Usage Page (Button)
        0x95, 0x0B,        //     Report Count (11)
        0x29, 0x0B,        //     Usage Maximum (0x0B)
        0x05, 0x09,        //     Usage Page (Button)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
        0x95, 0x01,        //     Report Count (1)
        0x75, 0x01,        //     Report Size (1)
        0x09, 0x02,        //     Usage (0x02)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
        0x75, 0x08,        //     Report Size (8)
        0x26, 0xFF, 0x00,  //     Logical Maximum (255)
        0x46, 0xFF, 0x00,  //     Physical Maximum (255)
        0x09, 0x31,        //     Usage (Y)
        0x09, 0x32,        //     Usage (Z)
        0x95, 0x02,        //     Report Count (2)
        0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0xC0,              //   End Collection
        0xA1, 0x02,        //   Collection (Logical)
        0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
        0x95, 0x07,        //     Report Count (7)
        0x09, 0x03,        //     Usage (0x03)
        0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0xC0,              //   End Collection
        0x0A, 0xFF, 0xFF,  //   Usage (0xFFFF)
        0x95, 0x08,        //   Report Count (8)
        0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0xC0,              // End Collection
};

#define MAKE_RDESC(USB_PRODUCT_ID, RDESC) \
        { USB_VENDOR_ID_LOGITECH, USB_PRODUCT_ID, RDESC,  sizeof(RDESC) }

static struct {
    unsigned short vendor;
    unsigned short product;
    unsigned char * rdesc;
    unsigned short length;
} rdesc_fixed[] = {
        MAKE_RDESC(USB_PRODUCT_ID_LOGITECH_FORMULA_FORCE_GP, ffgp_rdesc_fixed),
        MAKE_RDESC(USB_PRODUCT_ID_LOGITECH_DRIVING_FORCE,    df_rdesc_fixed),
        MAKE_RDESC(USB_PRODUCT_ID_LOGITECH_MOMO_WHEEL,       momo_rdesc_fixed),
        MAKE_RDESC(USB_PRODUCT_ID_LOGITECH_MOMO_WHEEL2,      momo2_rdesc_fixed),
        MAKE_RDESC(USB_PRODUCT_ID_LOGITECH_VIBRATION_WHEEL,  fv_rdesc_fixed),
        MAKE_RDESC(USB_PRODUCT_ID_LOGITECH_DFP_WHEEL,        dfp_rdesc_fixed),
        MAKE_RDESC(USB_PRODUCT_ID_LOGITECH_WII_WHEEL,        wii_rdesc_fixed),
};

static void fix_rdesc(s_hid_info * hid_info) {

    unsigned int i;
    for (i = 0; i < sizeof(rdesc_fixed) / sizeof(*rdesc_fixed); ++i) {
        if (rdesc_fixed[i].vendor == hid_info->vendor_id && rdesc_fixed[i].product == hid_info->product_id) {
            hid_info->reportDescriptor = rdesc_fixed[i].rdesc;
            hid_info->reportDescriptorLength = rdesc_fixed[i].length;
        }
    }
}
#endif

typedef struct
{
    unsigned short product_id;
    unsigned char command[FF_LG_OUTPUT_REPORT_SIZE];
} s_native_mode;

static s_native_mode native_modes[] =
{
    { USB_PRODUCT_ID_LOGITECH_DFGT_WHEEL,   { 0x00, 0xf8, 0x09, 0x03, 0x01 } },
    { USB_PRODUCT_ID_LOGITECH_G27_WHEEL,    { 0x00, 0xf8, 0x09, 0x04, 0x01 } },
    { USB_PRODUCT_ID_LOGITECH_G25_WHEEL,    { 0x00, 0xf8, 0x10 } },
    { USB_PRODUCT_ID_LOGITECH_DFP_WHEEL,    { 0x00, 0xf8, 0x01 } },
    { USB_PRODUCT_ID_LOGITECH_G29_PC_WHEEL, { 0x00, 0xf8, 0x09, 0x05, 0x01, 0x01 } },
};

static s_native_mode * get_native_mode_command(unsigned short product, unsigned short bcdDevice)
{
  unsigned short native = 0x0000;

  if(((USB_PRODUCT_ID_LOGITECH_DRIVING_FORCE == product) || (USB_PRODUCT_ID_LOGITECH_DFP_WHEEL == product)
          || (USB_PRODUCT_ID_LOGITECH_DFGT_WHEEL == product) || (USB_PRODUCT_ID_LOGITECH_G25_WHEEL == product) || (USB_PRODUCT_ID_LOGITECH_G27_WHEEL == product))
          && ((0x1350 == (bcdDevice & 0xfff8) || 0x8900 == (bcdDevice & 0xff00)))) {
    native = USB_PRODUCT_ID_LOGITECH_G29_PC_WHEEL;
  } else if(((USB_PRODUCT_ID_LOGITECH_DRIVING_FORCE == product) || (USB_PRODUCT_ID_LOGITECH_DFP_WHEEL == product))
      && (0x1300 == (bcdDevice & 0xff00))) {
    native = USB_PRODUCT_ID_LOGITECH_DFGT_WHEEL;
  } else if(((USB_PRODUCT_ID_LOGITECH_DRIVING_FORCE == product) || (USB_PRODUCT_ID_LOGITECH_DFP_WHEEL == product) || (USB_PRODUCT_ID_LOGITECH_G25_WHEEL == product))
      && (0x1230 == (bcdDevice & 0xfff0))) {
    native = USB_PRODUCT_ID_LOGITECH_G27_WHEEL;
  } else if(((USB_PRODUCT_ID_LOGITECH_DRIVING_FORCE == product) || (USB_PRODUCT_ID_LOGITECH_DFP_WHEEL == product))
      && (0x1200 == (bcdDevice & 0xff00))) {
    native = USB_PRODUCT_ID_LOGITECH_G25_WHEEL;
  } else if((USB_PRODUCT_ID_LOGITECH_DRIVING_FORCE == product)
      && (0x1000 == (bcdDevice & 0xf000))) {
    native = USB_PRODUCT_ID_LOGITECH_DFP_WHEEL;
  }

  unsigned int i;
  for (i = 0; i < sizeof(native_modes) / sizeof(*native_modes); ++i) {
    if (native_modes[i].product_id == native) {
      return native_modes + i;
    }
  }

  return NULL;
}

#ifndef WIN32
static int send_native_mode(const struct ghid_device_info * dev, const s_native_mode * native_mode) {

    struct ghid_device * device = ghid_open_path(dev->path);
    if (device == NULL) {
        return -1;
    }
    int ret = ghid_write_timeout(device, native_mode->command, sizeof(native_mode->command), 1000);
    if (ret <= 0) {
        if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
            fprintf(stderr, "failed to send native mode command for HID device %s (PID=%04x)\n", dev->path, dev->product_id);
        }
        ret = -1;
    } else {
        if (GLOG_LEVEL(GLOG_NAME,INFO)) {
            printf("native mode command sent to HID device %s (PID=%04x)\n", dev->path, dev->product_id);
        }
        ret = 0;
    }
    ghid_close(device);
    return ret;
}

static int check_native_mode(const struct ghid_device_info * dev, unsigned short product_id) {

    // wait up to 5 seconds for the device to enable native mode
    int reset = 0;
    int cpt = 0;
    do {
        // sleep 1 second between each retry
        int i;
        for (i = 0; i < 10; ++i) {
            usleep(100000);
        }
        ++cpt;
        struct ghid_device_info * hid_devs = ghid_enumerate(USB_VENDOR_ID_LOGITECH, product_id);
        struct ghid_device_info * current;
        for (current = hid_devs; current != NULL && reset == 0; current = current->next) {
            // Warning: this only works on GNU/Linux, on Windows the device path is expected to change
            if (strcmp(current->path, dev->path) == 0) {
                if (GLOG_LEVEL(GLOG_NAME,INFO)) {
                    printf("native mode enabled for HID device %s (PID=%04x)\n", dev->path, product_id);
                }
                reset = 1;
            }
        }
        ghid_free_enumeration(hid_devs);
    } while (cpt < 5 && !reset);

    return (reset == 1) ? 0 : -1;
}

static int set_native_mode(const struct ghid_device_info * dev, const s_native_mode * native_mode) {

    if (native_mode) {
        if (send_native_mode(dev, native_mode) < 0) {
            return -1;
        }
        if (check_native_mode(dev, native_mode->product_id) < 0) {
            if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
                fprintf(stderr, "failed to enable native mode for HID device %s\n", dev->path);
            }
            return -1;
        }
    } else {
        if (GLOG_LEVEL(GLOG_NAME,INFO)) {
            printf("native mode is already enabled for HID device %s (PID=%04x)\n", dev->path, dev->product_id);
        }
    }
    return 0;
}
#else
static int set_native_mode(const struct ghid_device_info * dev __attribute__((unused)), const s_native_mode * native_mode) {

    if (native_mode) {
        if (GLOG_LEVEL(GLOG_NAME,INFO)) {
            printf("Found Logitech wheel not in native mode.\n");
        }
        const char * download = NULL;
        SYSTEM_INFO info;
        GetNativeSystemInfo(&info);
        switch (info.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64:
            case PROCESSOR_ARCHITECTURE_IA64:
            switch (native_mode->product_id)
            {
            case USB_PRODUCT_ID_LOGITECH_DFGT_WHEEL:
            case USB_PRODUCT_ID_LOGITECH_G27_WHEEL:
            case USB_PRODUCT_ID_LOGITECH_G25_WHEEL:
            case USB_PRODUCT_ID_LOGITECH_DFP_WHEEL:
                download = "https://gimx.fr/download/LGS64";
                break;
            case USB_PRODUCT_ID_LOGITECH_G29_PC_WHEEL:
            default:
                download = "https://gimx.fr/download/LGS64_2";
                break;
            }
            break;
            case PROCESSOR_ARCHITECTURE_INTEL:
            switch (native_mode->product_id)
            {
            case USB_PRODUCT_ID_LOGITECH_DFGT_WHEEL:
            case USB_PRODUCT_ID_LOGITECH_G27_WHEEL:
            case USB_PRODUCT_ID_LOGITECH_G25_WHEEL:
            case USB_PRODUCT_ID_LOGITECH_DFP_WHEEL:
                download = "https://gimx.fr/download/LGS32";
                break;
            case USB_PRODUCT_ID_LOGITECH_G29_PC_WHEEL:
            default:
                download = "https://gimx.fr/download/LGS32_2";
                break;
            }
            break;
        }
        if (download != NULL) {
            if (GLOG_LEVEL(GLOG_NAME,INFO)) {
                printf("Please install Logitech Gaming Software from: %s.\n", download);
            }
        }
    }
    return 0;
}
#endif

static struct hidinput_device_internal *  open_device(const struct ghid_device_info * dev) {

    s_native_mode * native_mode = get_native_mode_command(dev->product_id, dev->bcdDevice);
    if (set_native_mode(dev, native_mode) < 0) {
        return NULL;
    }

#ifndef WIN32
#ifdef UHID
    struct ghid_device * hid = ghid_open_path(dev->path);
    if (hid == NULL) {
        return NULL;
    }

    struct hidinput_device_internal * device = calloc(1, sizeof(*device));
    if (device == NULL) {
        PRINT_ERROR_ALLOC_FAILED("calloc");
        ghid_close(hid);
        return NULL;
    }

    device->hid = hid;

    GLIST_ADD(lgw_devices, device);

    const s_hid_info * hid_info = ghid_get_hid_info(device->hid);
    if (hid_info == NULL) {
        close_device(device);
        return NULL;
    }

    s_hid_info fixed_hid_info = *hid_info;

    // Some devices have a bad report descriptor, so fix it just like the kernel does.
    fix_rdesc(&fixed_hid_info);

    device->uhid = guhid_create(&fixed_hid_info, device->hid);
    if (device->uhid == NULL) {
        close_device(device);
        return NULL;
    }
    return device;
#else
    return NULL;
#endif
#else
    return NULL;
#endif
}

static struct ghid_device * get_hid_device(struct hidinput_device_internal * device) {

    return device->hid;
}

static s_hidinput_driver driver = {
        .ids = ids,
        .init = init,
        .open = open_device,
        .get_hid_device = get_hid_device,
        .process = process,
        .close = close_device,
};

void logitechwheel_constructor(void) __attribute__((constructor));
void logitechwheel_constructor(void) {
    if (hidinput_register(&driver) < 0) {
        exit(-1);
    }
}
