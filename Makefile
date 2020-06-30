obj-m = teapot.o
obj-m += ipc_tea.o

all:
	cp ../tea/include/driver/ipc/msg_defs.h .
	cp ../tea/include/config.h .
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean