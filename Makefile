TARGET = libequeue.a

CC ?= gcc
AR ?= ar
SIZE ?= size

SRC += $(wildcard *.c platforms/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)
ASM := $(SRC:.c=.s)

ifdef DEBUG
override CFLAGS += -O0 -g3
else
override CFLAGS += -Os
endif
ifdef WORD
override CFLAGS += -m$(WORD)
endif
override CFLAGS += -I.
override CFLAGS += -std=c99
override CFLAGS += -Wall
override CFLAGS += -D_XOPEN_SOURCE=600

override LFLAGS += -pthread


all: $(TARGET)

test: tests/tests.o $(OBJ)
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o tests/tests
	tests/tests

prof: tests/prof.o $(OBJ)
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o tests/prof
	tests/prof

asm: $(ASM)

size: $(OBJ)
	$(SIZE) -t $^

-include $(DEP)

%.a: $(OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) -c -MMD $(CFLAGS) $< -o $@

%.s: %.c
	$(CC) -S $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
	rm -f tests/tests tests/tests.o tests/tests.d
	rm -f tests/prof tests/prof.o tests/prof.d
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f $(ASM)
