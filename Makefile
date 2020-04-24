OBJECTS += $(patsubst %.c,%.o,$(wildcard src/*.c))
OBJECTS += $(patsubst %.c,%.o,$(wildcard src/hid/*.c))
ifneq ($(OS),Windows_NT)
OBJECTS += $(patsubst %.c,%.o,$(wildcard src/linux/*.c))
else
OBJECTS += $(patsubst %.c,%.o,$(wildcard src/windows/*.c))
OBJECTS += $(patsubst %.c,%.o,$(wildcard src/sdl/*.c))
endif

CPPFLAGS += -Iinclude -I. -I../
CFLAGS += -fPIC

LDFLAGS += -L../gimxlog
LDLIBS += -lgimxlog

LDFLAGS += -L../gimxhid
LDLIBS += -lgimxhid

LDFLAGS += -L../gimxtime
LDLIBS += -lgimxtime

ifeq ($(OS),Windows_NT)
CFLAGS += `sdl2-config --cflags`
LDLIBS += -lsetupapi -lws2_32
LDFLAGS += -L../gimxpoll
LDLIBS += -lgimxpoll
LDLIBS += `sdl2-config --libs`
else
ifeq ($(UHID),1)
CFLAGS += -DUHID
LDFLAGS += -L../gimxuhid
LDLIBS += -lgimxuhid
endif
LDLIBS += -lXi -lX11
endif

include Makedefs
