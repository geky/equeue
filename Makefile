TARGET = libevents.a


CC = gcc
AR = ar
SIZE = size

SRC += $(wildcard *.c sys/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)
ASM := $(SRC:.c=.s)

ifdef DEBUG
CFLAGS += -O0 -g3 -DMU_DEBUG
CFLAGS += -fkeep-inline-functions
else
CFLAGS += -O2
endif
ifdef WORD
CFLAGS += -m$(WORD)
endif
CFLAGS += -std=c99
CFLAGS += -Wall -Winline
CFLAGS += -D_POSIX_C_SOURCE=200112L

LFLAGS += -lpthread
LFLAGS += -lrt


all: $(TARGET)

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
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f $(ASM)
