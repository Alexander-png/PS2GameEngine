# gmrun: generic runner for converted GameMaker games.  make GEN=<dir with converter output> [EXTRA=-DHDD_BOOT ...]
ENG = ../../engine
GEN ?= ../../converter/out/gen_undertale
EE_BIN = gmrun.elf
GENC = $(sort $(wildcard $(GEN)/gm_code_*.c))
EE_OBJS = main.o room.o io.o gfx.o log.o input.o gml_core.o gml_inst.o gml_lib.o gml_file.o gml_coll.o gml_draw.o \
          gm_tables.o gm_objects.o gm_rooms.o gm_sprites.o $(patsubst $(GEN)/%.c,%.o,$(GENC)) fileXio_irx.o ps2hdd_irx.o ps2fs_irx.o \
          gml_audio.o gm_sounds.o audio.o freesd_irx.o engsnd_x_irx.o
EE_LIBS = -lfileXio -lpad -ldraw -lgraph -ldma -lpacket -ldebug -lcdvd -lpatches -lm -lc -lkernel
EE_CFLAGS += -O2 -G0 -DGM_REAL_FLOAT -fsingle-precision-constant -I$(ENG)/include -I$(GEN) $(EXTRA)
all: $(EE_BIN)
clean:
	rm -f $(EE_BIN) $(EE_OBJS) fileXio_irx.c ps2hdd_irx.c ps2fs_irx.c freesd_irx.c
%.o: $(ENG)/core/%.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
%.o: $(ENG)/platform/ps2/%.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
gm_code_%.o: $(GEN)/gm_code_%.c
	$(EE_CC) $(EE_CFLAGS) -Os $(EE_INCS) -c $< -o $@
%.o: $(GEN)/%.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
engsnd_x_irx.c: ../hwaudio/build/iop/engsnd.irx
	$(PS2SDK)/bin/bin2c $< $@ engsnd_x_irx
%_irx.c: $(PS2SDK)/iop/irx/%.irx
	$(PS2SDK)/bin/bin2c $< $@ $*_irx
%.o: %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
