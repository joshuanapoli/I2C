# Makefile
#
AS		= arm-elf-as
CC		= arm-elf-gcc
OBJCOPY	= arm-elf-objcopy

USE_THUMB_MODE	= YES
DEBUG			= -g
#DEBUG			=
OPTIM			= -O0
RUN_MODE		= RUN_FROM_ROM

LDSCRIPT	= LPC2103-rom.ld
RTOS		= FreeRTOS
PROJECT		= PROJECT

WARNINGS	= -Wall -Wextra -Wshadow -Wpointer-arith -Wbad-function-cast \
			  -Wcast-align -Wsign-compare -Waggregate-return -Wstrict-prototypes \
			  -Wmissing-prototypes -Wmissing-declarations -Wunused

AFLAGS	= -ahls -mapcs-32 -o $(PROJECT)/startup.o
CFLAGS	= \
		-I./$(PROJECT) \
		-I./$(PROJECT)/Include \
		-I./$(RTOS)/Source \
		-I./$(RTOS)/Source/Include \
		-I./$(RTOS)/Source/Portable \
		-mcpu=arm7tdmi \
		-T$(LDSCRIPT) \
		$(DEBUG) \
		$(OPTIM)

ifeq ($(USE_THUMB_MODE),YES)
	AFLAGS += -mthumb-interwork
	CFLAGS += -mthumb-interwork -D THUMB_INTERWORK
	THUMB_FLAGS=-mthumb
endif

LFLAGS	= -Xlinker -ocamera.elf -Xlinker -M -Xlinker -Map=camera.map

#
# Source files that can be built to THUMB mode
#
THUMB_SRC = \
$(PROJECT)/main.c \
$(PROJECT)/led.c \
$(PROJECT)/i2c.c \
$(PROJECT)/cam.c \
./$(RTOS)/Source/tasks.c \
./$(RTOS)/Source/queue.c \
./$(RTOS)/Source/list.c \
./$(RTOS)/Source/portable/MemMang/heap_2.c \
./$(RTOS)/Source/portable/port.c

#
# Source files that must be built to ARM mode
#
ARM_SRC = \
./$(RTOS)/Source/portable/portISR.c \
./$(PROJECT)/i2cISR.c

#
# Define all object files.
#
ARM_OBJ = $(ARM_SRC:.c=.o)
THUMB_OBJ = $(THUMB_SRC:.c=.o)

all:	camera.hex

camera.hex : camera.elf
	@ echo ""
	@ echo "Making camera.hex"
	$(OBJCOPY) camera.elf -O ihex camera.hex

camera.elf : $(ARM_OBJ) $(THUMB_OBJ) startup.o Makefile
	@ echo ""
	@ echo "Compiling and linking..."
	$(CC) $(CFLAGS) $(ARM_OBJ) $(THUMB_OBJ) -nostartfiles $(PROJECT)/startup.o $(LFLAGS)

startup.o: $(PROJECT)/startup.s
	@ echo ""
	@ echo "Assembling startup.s"
	$(AS) $(AFLAGS)	$(PROJECT)/startup.s >	$(PROJECT)/startup.lst
	
$(THUMB_OBJ) : %.o : %.c $(LDSCRIPT) Makefile
	$(CC) -c $(THUMB_FLAGS) $(CFLAGS) $< -o $@

$(ARM_OBJ) : %.o : %.c $(LDSCRIPT) Makefile
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	@ echo ""
	@ echo "Cleaning files"
	-rm	camera.hex \
		camera.map \
		camera.elf \
		$(THUMB_OBJ) \
		$(ARM_OBJ) \
		$(PROJECT)/startup.lst \
		$(PROJECT)/startup.o

# End Makefile