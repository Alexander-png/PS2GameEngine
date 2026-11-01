# engine smoke test: core + ps2 platform module
ENG = ../../engine
EE_BIN = roomtest.elf
EE_OBJS = main.o room.o io.o gfx.o log.o input.o fileXio_irx.o ps2hdd_irx.o ps2fs_irx.o
EE_LIBS = -lfileXio -lpad -ldraw -lgraph -ldma -lpacket -ldebug -lcdvd -lpatches -lm -lc -lkernel
EE_CFLAGS += -O2 -I$(ENG)/include $(EXTRA)
all: $(EE_BIN)
clean:
	rm -f $(EE_BIN) $(EE_OBJS) *_irx.c
room.o: $(ENG)/core/room.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
input.o: $(ENG)/platform/ps2/input.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
log.o: $(ENG)/core/log.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
io.o: $(ENG)/platform/ps2/io.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
gfx.o: $(ENG)/platform/ps2/gfx.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
%_irx.c: $(PS2SDK)/iop/irx/%.irx
	$(PS2SDK)/bin/bin2c $< $@ $*_irx
%.o: %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
