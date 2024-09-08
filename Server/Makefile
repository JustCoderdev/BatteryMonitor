# JustCoderdev Makefile for C projects v6

PNAME = BatteryMonitor_Server

SRC_FILES  = main.c
OBJ_FILES  = $(patsubst %.c,%.o,${SRC_FILES})
# SHS_FILES  = CoreMountain/shs/shs.c
CORE_FILES = CoreMountain/core/core_bit.c \
             CoreMountain/core/core_buff.c \
             CoreMountain/core/core_logger.c \
             CoreMountain/core/core_memdeb.c \
             CoreMountain/core/core_net.c \
             CoreMountain/core/core_str.c \
             CoreMountain/core/core_stuff.c

CC = gcc
CCFLAGS = -xc -std=c89 -ansi -pedantic-errors -pedantic \
		 -Wall -Wextra -Werror -Wshadow -Wpointer-arith \
		 -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes \
		 -Wconversion -g

IFLAGS = -I./ -I./lib/include -I./CoreMountain/include
LDFLAGS = -L./
#RAYFLAGS = -lGL -lm -lpthread -ldl -lX11 \
#		-Wl,-rpath=./lib/raylib/ -L./lib/raylib -lraylib
##-lXrandr -lXinerama -lXi -lXcursor

# -DDEBUG_STRING_ENABLE=1
# -DDEBUG_MEMDEB_ENABLE=1
DFLAGS = -DDEBUG_ENABLE=1 -DSGR_ENABLE=1
FLAGS = $(CCFLAGS) $(IFLAGS) $(LDFLAGS) $(RAYFLAGS) $(DFLAGS)

local: build
build:
	@echo "Compiling... "
	@mkdir -p bin
	$(CC) $(FLAGS) $(SRC_FILES) $(CORE_FILES) -o bin/${PNAME}

run:
	@echo -e "Running...\n"
	@chmod +x bin/$(PNAME)
	@cd bin && ./$(PNAME)


.PHONY: clean
clean:
	@echo "Cleaning..."
	@rm -rf ./bin


# raycompile: rayclean raybuild
# rayclean:
# 	cd lib/sources/raylib/src && make clean

# raybuild:
# 	cd lib/sources/raylib/src && make PLATFORM=PLATFORM_DESKTOP GLFW_LINUX_ENABLE_WAYLAND=OFF

# rayshared:
# 	cd lib/sources/raylib/src && make PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=SHARED GLFW_LINUX_ENABLE_WAYLAND=OFF
