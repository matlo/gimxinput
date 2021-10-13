

#ifndef SHMMOUSE_H_
#define SHMMOUSE_H_

#include <ginput.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxcommon/include/gerror.h>
#include "../events.h"


int shminput_grab(int mode);
int shminput_init(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*), int mouse_num, int fd);
int shminput_close(void * user __attribute__((unused)));


#endif