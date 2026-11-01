IOP_BIN = engsnd.irx
IOP_OBJS = engsnd.o imports.o
include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
.DEFAULT_GOAL := all
all: $(IOP_BIN)
clean:
	rm -rf $(IOP_BIN) *.elf obj
