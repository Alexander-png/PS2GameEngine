EE_BIN = audiotest.elf
EE_OBJS = main.o testaudio_bin.o panaudio_bin.o engsnd_irx.o freesd_irx.o
EE_LIBS = -ldebug -lpatches -lcdvd -lc -lkernel
EE_CFLAGS += -O2 $(EXTRA)
all: $(EE_BIN)
clean:
	rm -f $(EE_BIN) $(EE_OBJS)
%.o: %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
