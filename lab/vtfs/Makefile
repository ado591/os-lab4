obj-m += source/vtfs.o 

PWD := $(CURDIR) 
KDIR = /lib/modules/`uname -r`/build
EXTRA_CFLAGS = -Wall -g

CLANG_FORMAT ?= clang-format
CLANG_TIDY   ?= clang-tidy

SRC_FILES := source/vtfs.c source/vtfs.h

all:
	make -C $(KDIR) M=$(PWD) modules 

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -rf .cache

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SRC_FILES)

tidy:
	$(CLANG_TIDY) source/vtfs.c \
		--extra-arg=-D__KERNEL__ \
		--extra-arg=-DMODULE \
		-- \
		-I$(KDIR)/include \
		-I$(PWD)
		
check: format-check tidy
