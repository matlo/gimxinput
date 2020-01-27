/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>

#include <gimxinput/include/ginput.h>
#include <gimxpoll/include/gpoll.h>
#include <gimxtimer/include/gtimer.h>
#include <gimxprio/include/gprio.h>
#include <gimxlog/include/glog.h>

#include <gimxcommon/test/common.h>
#include <gimxcommon/test/handlers.c>
#include <gimxcommon/test/input.c>
#include <gimxcommon/test/timer.c>

#include <gimxcommon/include/gperf.h>

#define SAMPLETYPE \
    struct { \
            gtime now; \
            gtime delta; \
        }

#define NBSAMPLES 2500 // 10s with a period of 4ms

#define SAMPLEPRINT(SAMPLE) \
    printf("now = "GTIME_FS" delta = "GTIME_FS"\n", SAMPLE.now, SAMPLE.delta)

static GPERF_INST(ginput_test, SAMPLETYPE, NBSAMPLES);

#define PERIOD 10000//microseconds

int mkb_select() {

  printf("Available mouse and keyboard input methods:\n");
  printf("%d none\n", GE_MKB_SOURCE_NONE);
  printf("%d physical\n", GE_MKB_SOURCE_PHYSICAL);
  printf("%d window system\n", GE_MKB_SOURCE_WINDOW_SYSTEM);

  printf("Select the input method: ");
  fflush(stdout);
  unsigned int choice = UINT_MAX;
  if (scanf("%d", &choice) == 1 && choice <= GE_MKB_SOURCE_WINDOW_SYSTEM) {
    return choice;
  }

  fprintf(stderr, "Invalid choice.\n");
  return -1;
}

int process_event2(GE_Event* event __attribute__((unused))) {
    if (event->type == GE_KEYDOWN && event->key.keysym == GE_KEY_ESC)
    {
        done = 1;
        fflush(stdout);
        return 1;
    }
    return 0;
}

static void usage() {
  fprintf(stderr, "Usage: ./ginput_test [-d] [-n period_count] [-p] [-q]\n");
  exit(EXIT_FAILURE);
}

static unsigned int periods = 0;
static int quiet = 0;
static int debug = 0;
static int prio = 0;
static int perf = 0;

/*
 * Reads command-line arguments.
 */
static int read_args(int argc, char* argv[]) {

  int opt;
  while ((opt = getopt(argc, argv, "dn:pqs")) != -1) {
    switch (opt) {
    case 'd':
      debug = 1;
      break;
    case 'n':
      periods = atoi(optarg);
      break;
    case 'p':
      prio = 1;
      break;
    case 'q':
      quiet = 1;
      break;
    case 's':
      perf = 1;
      break;
    default: /* '?' */
      usage();
      break;
    }
  }
  return 0;
}

int main(int argc __attribute__((unused)), char* argv[] __attribute__((unused)))
{
  setup_handlers();

  read_args(argc, argv);

  if (debug)
  {
    glog_set_all_levels(E_GLOG_LEVEL_DEBUG);
  }

  int mkb_source = mkb_select();

  if (mkb_source < 0)
  {
    exit(-1);
  }

  GPOLL_INTERFACE poll_interface = {
          .fp_register = REGISTER_FUNCTION,
          .fp_remove = REMOVE_FUNCTION
  };
  if (ginput_init(&poll_interface, mkb_source, quiet ? process_event2 : process_event) < 0)
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

  if (prio && gprio_init() < 0)
  {
    exit(-1);
  }

  while(!is_done() && gperf_ginput_test.count != NBSAMPLES)
  {
    gpoll();

    if (perf) {
      GPERF_START(ginput_test);
    }

    ginput_periodic_task();

    if (perf) {
      GPERF_END(ginput_test);
    }

    //do something periodically

    if (periods > 0) {
        --periods;
        if (periods == 0) {
            set_done();
        }
    }
  }

  if (prio)
  {
    gprio_clean();
  }

  if (timer != NULL) {
    gtimer_close(timer);
  }

  ginput_quit();

  printf("Exiting\n");

  if (perf) {
    GPERF_SAMPLE_PRINT(ginput_test, SAMPLEPRINT);
    GPERF_LOG(ginput_test);
  }

  return 0;
}
