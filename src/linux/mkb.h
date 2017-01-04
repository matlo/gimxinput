/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef MKB_H_
#define MKB_H_

#include <ginput.h>
#include <gimxpoll/include/gpoll.h>

int mkb_init(const GPOLL_INTERFACE * poll_interface, int (*callback)(GE_Event*));
int mkb_close_device(int id);
void mkb_quit();
void mkb_grab(int mode);
char * mkb_get_k_name(int index);
char * mkb_get_m_name(int index);

#endif /* MKB_H_ */
