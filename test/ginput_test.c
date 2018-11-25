/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include <gimxinput/include/ginput.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxtimer/include/gtimer.h>

#include <gimxcommon/test/common.h>
#include <gimxcommon/test/handlers.c>
#include <gimxcommon/test/input.c>
#include <gimxcommon/test/timer.c>

#define PERIOD 10000//microseconds

#ifdef WIN32
#define GE_MKB_SOURCE_MAX GE_MKB_SOURCE_LOW_LEVEL_HOOKS
#else
#define GE_MKB_SOURCE_MAX GE_MKB_SOURCE_WINDOW_SYSTEM
#endif

int mkb_select() {

  printf("Available mouse and keyboard input methods:\n");
  printf("%d none\n", GE_MKB_SOURCE_NONE);
  printf("%d physical\n", GE_MKB_SOURCE_PHYSICAL);
  printf("%d window system\n", GE_MKB_SOURCE_WINDOW_SYSTEM);
#ifdef WIN32
  printf("%d low level hooks\n", GE_MKB_SOURCE_LOW_LEVEL_HOOKS);
#endif

  printf("Select the input method: ");
  fflush(stdout);
  unsigned int choice = UINT_MAX;
  if (scanf("%d", &choice) == 1 && choice <= GE_MKB_SOURCE_MAX) {
    return choice;
  }

  fprintf(stderr, "Invalid choice.\n");
  return -1;
}

int main(int argc __attribute__((unused)), char* argv[] __attribute__((unused)))
{
  setup_handlers();

  int mkb_source = mkb_select();

  if (mkb_source < 0)
  {
    exit(-1);
  }

  GPOLL_INTERFACE poll_interface = {
          .fp_register = REGISTER_FUNCTION,
          .fp_remove = REMOVE_FUNCTION
  };
  if (ginput_init(&poll_interface, mkb_source, process_event) < 0)
  {
    exit(-1);
  }

  display_devices();

  GTIMER_CALLBACKS timer_callbacks = {
          .fp_read = timer_read,
          .fp_close = timer_close,
          .fp_register = REGISTER_FUNCTION,
          .fp_remove = REMOVE_FUNCTION,
  };
  struct gtimer * timer = gtimer_start(NULL, PERIOD, &timer_callbacks);
  if (timer == NULL) {
    set_done();
  }
  
  if (mkb_source != GE_MKB_SOURCE_NONE) {
    ginput_grab();
  }

  while(!is_done())
  {
    gpoll();

    ginput_periodic_task();

    //do something periodically
  }

  if (timer != NULL) {
    gtimer_close(timer);
  }

  ginput_quit();

  printf("Exiting\n");

  return 0;
}
