obj-m := src/linux_sense.o

VER  ?= $(shell uname -r)
DIR  ?= /lib/modules/$(VER)/build
PWD   := $(shell pwd)

CC      := clang
LD      := ld.lld

all:
	$(MAKE) -C $(DIR) M=$(PWD) CC=$(CC) LD=$(LD) modules

clean:
	$(MAKE) -C $(DIR) M=$(PWD) CC=$(CC) LD=$(LD) clean