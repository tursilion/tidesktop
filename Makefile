# TI-99/4A Desktop Environment Makefile
# Based on gcc9900 toolchain

# Paths to TMS9900 compilation tools (from Makefile.rick)
TMS9900_DIR?=/home/tursi/newtms9900-gcc/newgcc9900/bin
ELF2EA5_DIR?=/home/tursi/gcc9900/bin
CLASSIC99_DSK1?=/mnt/d/classic99/DSK1/
CLASSIC99_PASTE?=/mnt/d/work/classic99paste/release/classic99paste.exe
LIBTI99?=/mnt/d/work/libti99ALL/

# Executables
GAS=$(TMS9900_DIR)/tms9900-as
LD=$(TMS9900_DIR)/tms9900-ld
CC=$(TMS9900_DIR)/tms9900-gcc
ELF2EA5=$(ELF2EA5_DIR)/elf2ea5

CP=/usr/bin/cp
NAME=desktop

# Compiler flags
# -Os: optimize for size
# -std=c99: use C99 standard
# -fno-builtin: don't use built-in functions
# -fno-function-cse: prevents gcc from preferring registers for bl calls
CFLAGS=-Os -std=c99 -c -s --save-temp -DTI99 -fno-builtin -fno-function-cse -fverbose-asm

# Include paths
INCPATH=-I$(LIBTI99)

# Library paths - use buildti subdirectory for TI-specific build
LIBPATH=-L$(LIBTI99)/buildti
LIBS=-lti99

# Linker flags - text at >A000 (high expansion), data at >2000 (low expansion)
LDFLAGS=-M -Ttext=0xA000 -Tdata=0x2000

.PHONY: all clean install test

# Object files - crt0 must be first!
OBJECTS = crt0.o main.o chars.o ui.o input.o device.o window.o viewer.o prefs.o scratchloaderDesktop.o

all: $(NAME)

$(NAME): $(OBJECTS)
	$(LD) $(OBJECTS) $(LIBS) $(LIBPATH) $(LDFLAGS) -o $(NAME).elf > $(NAME).map
	$(ELF2EA5) $(NAME).elf $(NAME)

clean:
	-rm -f *.o *.elf *.map DESKTOP* TEST* *.s

install: $(NAME)
	$(CP) DESKTOP* $(CLASSIC99_DSK1)
	$(CLASSIC99_PASTE) -reset QQ25DSK0.DESKTOP1\\n

# Local crt0 for EA5 startup (copied from libti99)
crt0.o: crt0.asm
	$(GAS) $< -o $@

# Assembly files
%.o: %.asm
	$(GAS) $< -o $@

# C files
%.o: %.c
	$(CC) -c $< $(CFLAGS) $(INCPATH) -o $@

# Test program - uses files.h to open a non-existent file
test: crt0.o test.o
	$(LD) crt0.o test.o $(LIBS) $(LIBPATH) $(LDFLAGS) -o test.elf > test.map
	$(ELF2EA5) test.elf test
	$(CP) TEST* $(CLASSIC99_DSK1)
