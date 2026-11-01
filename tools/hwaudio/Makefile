# hwaudio: audio test for real hardware (HDD, no IOP reset). engsnd is rebuilt with iomanX file access so it can stream from pfs0:
ENG = ../../engine
AT = ../audiotest
EE_BIN = hwaudio.elf
EE_OBJS = main.o io.o log.o input.o fileXio_irx.o ps2hdd_irx.o ps2fs_irx.o engsnd_x_irx.o freesd_irx.o
EE_LIBS = -lfileXio -lpad -ldebug -lcdvd -lpatches -lm -lc -lkernel
EE_CFLAGS += -O2 -I$(ENG)/include $(EXTRA)
all: $(EE_BIN)
clean:
	rm -rf $(EE_BIN) $(EE_OBJS) *_irx.c build
build/iop/engsnd.irx: $(AT)/iop/engsnd.c $(AT)/iop/irx_imports.h $(AT)/iop/imports.lst
	rm -rf build/iop; mkdir -p build/iop
	cp $(AT)/iop/engsnd.c $(AT)/iop/irx_imports.h $(AT)/iop/Makefile build/iop/
	python3 -c "s=open('$(AT)/iop/imports.lst').read().replace('ioman_IMPORTS_start\nI_open\nI_close\nI_read\nI_lseek\nioman_IMPORTS_end','iomanX_IMPORTS_start\nI_iomanX_open\nI_iomanX_close\nI_iomanX_read\nI_iomanX_lseek\niomanX_IMPORTS_end'); open('build/iop/imports.lst','w').write(s)"
	cd build/iop && IOP_CFLAGS=-DENGSND_IOMANX $(MAKE) engsnd.irx
engsnd_x_irx.c: build/iop/engsnd.irx
	$(PS2SDK)/bin/bin2c $< $@ engsnd_x_irx
freesd_irx.c: $(PS2SDK)/iop/irx/freesd.irx
	$(PS2SDK)/bin/bin2c $< $@ freesd_irx
io.o: $(ENG)/platform/ps2/io.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
log.o: $(ENG)/core/log.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
input.o: $(ENG)/platform/ps2/input.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
%_irx.c: $(PS2SDK)/iop/irx/%.irx
	$(PS2SDK)/bin/bin2c $< $@ $*_irx
%.o: %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
