# build nimbs
CC := gcc
CFLAGS := -g -std=c11 -O3 -Wall
LDFLAGS := -ljansson
SOURCES_DIRS := ./src
SOURCES := $(wildcard $(addsuffix /*.c, $(SOURCES_DIRS)))
OBJECTS := $(notdir $(SOURCES))
OBJECTS := $(OBJECTS:.c=.o)
EXECUTABLE := nimbs

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS) -pipe 

VPATH := $(SOURCES_DIRS)

%.o: %.c
	$(CC) $< $(CFLAGS) -c -MD -pipe

clean:
	rm -rf *.o *.d $(EXECUTABLE)

.PHONY: all clean install uninstall

include $(wildcard *.d)
