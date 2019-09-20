ifneq ($(DEBUG),1)
CFLAGS += -Wall -Wextra -O3
CXXFLAGS += -Wall -Wextra -O3
else
ifneq ($(OS),Windows_NT)
CFLAGS += -Wall -Wextra -O0 -g -fsanitize=address -fno-omit-frame-pointer
CXXFLAGS += -Wall -Wextra -O0 -g -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
else
CFLAGS += -Wall -Wextra -O0 -g -fno-omit-frame-pointer
CXXFLAGS += -Wall -Wextra -O0 -g -fno-omit-frame-pointer
endif
endif

CPPFLAGS += -I../..

LDFLAGS += -L../../gimxinput -L../../gimxhid -L../../gimxpoll -L../../gimxlog -L../../gimxtime -L../../gimxtimer
LDLIBS += -lgimxinput -lgimxhid -lgimxpoll -lgimxlog -lgimxtime -lgimxtimer

BINS=ginput_test ginput_haptic_test
ifneq ($(OS),Windows_NT)
OUT=$(BINS)
else
OUT=ginput_test.exe ginput_haptic_test.exe
endif

ifeq ($(OS),Windows_NT)
CPPFLAGS += `sdl2-config --cflags`
LDLIBS += $(shell sdl2-config --libs) -lws2_32 -lwinmm -lhid -lsetupapi
LDLIBS:=$(filter-out -mwindows,$(LDLIBS))
else
LDFLAGS += -L../../gimxuhid
LDLIBS += -lgimxuhid -lXi -lX11
endif

all: $(BINS)

clean:
	$(RM) $(OUT) *~
