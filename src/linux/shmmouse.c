#include "shmmouse.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/shm.h>
#include <ginput.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxcommon/include/gerror.h>
#include <gimxcommon/include/glist.h>
#include <gimxlog/include/glog.h>
#include "../events.h"

GLOG_GET(GLOG_NAME)

typedef struct 
{
    uint8_t buttons;
    int16_t x;
    int16_t y;
    uint8_t changed;
}shminput_t;

typedef struct 
{
  char* name;
  shminput_t* inputarray; 
  uint8_t lastbuttons;
  int16_t lastX;
  int16_t lastY;
  int16_t remainingX;
  int16_t remainingY;
  int mouse_num;
  int fd;
}shminput_device_t;

static shminput_device_t * shm_device = NULL;
static int (*event_callback)(GE_Event*) = NULL;
static GPOLL_REMOVE_FD fp_remove = NULL;

int shminput_close(void * user __attribute__((unused))) {
    shminput_device_t * device = shm_device; 
    if (device == NULL)
        return 1; // already free
    if (device->inputarray != NULL)
    {
        shmdt(device->inputarray);
        device->inputarray = NULL;
    }

    free(device);
    shm_device = NULL;

    return 1;
}

static int16_t decreaseXY(int16_t* XY) {
    /*
        Interface passes 16 bit int values but mouse outputs -127to+127
        If input is greater then limits then output 127 and save remainder for next poll
    */
    if (*XY==0)
        return 0;
    if (*XY>0) 
    {
        if (*XY>127)
        {
            *XY=*XY-127;
            return 127;
        }
        else 
        {
            int16_t cur= *XY;
            *XY=0; // nothing left to send
            return cur;
        }
    }
    else
    {
        if (*XY<-127)
        {
            *XY=*XY+127;
            return -127;
        }
        else 
        {
            int16_t cur= *XY;
            *XY=0; // nothing left to send
            return cur;
        }
    }
}

static int shminput_check_events(void * user __attribute__((unused))) {
    // Only 1 shm device so always use static variable
    shminput_device_t * device = shm_device;
    if (device->inputarray->changed)
    {
        // do work here
        //GE_Event evt = { };
        if (device->lastbuttons != device->inputarray->buttons)
        {
            // buttons changed   
            //not implemented yet

        }
        if (device->lastX != device->inputarray->x)
        {
            // x changed
            device->remainingX = device->inputarray->x;
            device->lastX = device->inputarray->x;
        }
        if (device->lastY != device->inputarray->y)
        {
            // y changed
            device->remainingY = device->inputarray->y;
            device->lastY = device->inputarray->y;
        }
        // reset changed flag
        device->inputarray->changed = 0;
        
        // See if we need to send a movent update
        if ((device->remainingX !=0)|(device->remainingY !=0))
        {
            // send update
            GE_Event evt = { };  
            evt.motion.which = device->mouse_num;
            evt.type = GE_MOUSEMOTION;
            evt.motion.xrel = decreaseXY(&device->remainingX);
            evt.motion.yrel = decreaseXY(&device->remainingY);
            event_callback(&evt);
        }
    }
    return 0;
}

int shminput_grab(int mode) {

    // grab is always on
    return mode;

}


int shminput_init(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*), int mouse_num, int fd) {

    if (callback == NULL) {
        PRINT_ERROR_OTHER("callback is NULL");
        return -1;
    }

    if (poll_interface->fp_register == NULL) {
        PRINT_ERROR_OTHER("fp_register is NULL");
        return -1;
    }

    if (poll_interface->fp_remove == NULL) {
        PRINT_ERROR_OTHER("fp_remove is NULL");
        return -1;
    }

    event_callback = callback;
    fp_remove = poll_interface->fp_remove;

    int shm_id;
    if (0 >  (shm_id = shmget(454, sizeof(shminput_t), 0660)))
    {
        // shm not created in other app
        PRINT_ERROR_OTHER("SHM Mouse: Unable to allocate shared memory");
        return -2;
    }
    
    shminput_device_t * device = shm_device = calloc(1, sizeof(shminput_device_t));
    if (device == NULL) {
        PRINT_ERROR_ALLOC_FAILED("calloc");
        return -1;
    }

    device->inputarray = (shminput_t *) shmat(shm_id,NULL,0);
    device->name = "SHM Mouse";
    device->inputarray->changed =0; // clear existing events
    device->mouse_num = mouse_num;
    device->fd = fd;
    
    // register callback
    GPOLL_CALLBACKS callbacks = {
        .fp_read = shminput_check_events,
        .fp_write = NULL,
        .fp_close = shminput_close,
        };
    
    poll_interface->fp_register(device->fd, NULL, &callbacks);
    return 0;
}