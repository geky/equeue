TARGET = libevents.a

CC = gcc
AR = ar
SIZE = size

SRC += $(wildcard *.c)
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)
ASM := $(SRC:.c=.s)

TESTS = tests/tests
TSRC += $(wildcard tests/*.c)
TOBJ := $(TSRC:.c=.o)
TDEP := $(TSRC:.c=.d)

ifdef DEBUG
CFLAGS += -O0 -g3 -DMU_DEBUG
CFLAGS += -fkeep-inline-functions
else
CFLAGS += -O2
endif
ifdef WORD
CFLAGS += -m$(WORD)
endif
CFLAGS += -I.
CFLAGS += -std=c99
CFLAGS += -Wall -Winline
CFLAGS += -D_XOPEN_SOURCE=500

LFLAGS += -lpthread


all: $(TARGET)

test: $(TOBJ) $(OBJ)
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $(TESTS)
	$(TESTS)

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
	rm -f $(TESTS)
	rm -f $(TOBJ) $(TDEP)
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f $(ASM)
