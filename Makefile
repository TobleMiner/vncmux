CC ?= gcc
RM = rm -f

CCFLAGS = -Wall -D_GNU_SOURCE
ifeq ($(DEBUG),1)
	CCFLAGS += -O1 -ggdb
else
	CCFLAGS += -Ofast -march=native
endif

DEPS = libvncserver libvncclient
DEPFLAGS_CC = `pkg-config --cflags $(DEPS)`
DEPFLAGS_LD = `pkg-config --libs $(DEPS)` -lpthread
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))
HDRS = $(wildcard *.h)

all: vncmux

%.o : %.c $(HDRS) Makefile
	$(CC) -c $(CCFLAGS) $(DEPFLAGS_CC) $< -o $@

vncmux: $(OBJS)
	$(CC) $(CCFLAGS) $^ $(DEPFLAGS_LD) -o vncmux

clean:
	$(RM) $(OBJS)
	$(RM) vncmux

.PHONY: all clean
