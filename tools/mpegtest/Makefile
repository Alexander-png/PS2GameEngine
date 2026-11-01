# mpegtest: libmpeg hardware test. make            -> mpegtest.elf (libmpeg from the vendored SDK, with the fix)
#                                  make ORIG=1       -> mpegtest_orig.elf (unpatched upstream libmpeg, needs ./build_orig_lib.sh once)
# Test streams: ./gen_streams.sh (synthetic, made with ffmpeg; no game content).
ENG = ../../engine
ifdef ORIG
EE_BIN = mpegtest_orig.elf
MPEGLIB = -Lbuild/orig -lmpegorig
EXTRA += -DORIG
else
EE_BIN = mpegtest.elf
MPEGLIB = -lmpeg
endif
EE_OBJS = main.o io.o log.o s352_m2v.o s640_m2v.o fileXio_irx.o ps2hdd_irx.o ps2fs_irx.o
EE_LIBS = -lfileXio -ldraw -lgraph -ldma -lpacket $(MPEGLIB) -ldebug -lcdvd -lpatches -lc -lkernel
EE_CFLAGS += -O2 -G0 -I$(ENG)/include $(EXTRA)
all: $(EE_BIN)
clean:
	rm -f mpegtest.elf mpegtest_orig.elf $(EE_OBJS) *_irx.c s352_m2v.c s640_m2v.c
%.o: $(ENG)/core/%.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
%.o: $(ENG)/platform/ps2/%.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
%.o: %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
%_irx.c: $(PS2SDK)/iop/irx/%.irx
	$(PS2SDK)/bin/bin2c $< $@ $*_irx
s352_m2v.c: build/s352.m2v
	$(PS2SDK)/bin/bin2c $< $@ s352_m2v
s640_m2v.c: build/s640.m2v
	$(PS2SDK)/bin/bin2c $< $@ s640_m2v
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
