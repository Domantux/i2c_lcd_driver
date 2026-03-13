obj-m += lcd-driver.o
ccflags-y += -fno-stack-protector

ifndef KDIR
$(error KDIR is not set. Usage: make KDIR=/path/to/kernel/source)
endif

ARCH ?= arm
CROSS_COMPILE ?= arm-unknown-linux-musleabihf-

all:
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
