# Self-booting disc images that measure KOS init time on the console without dcload (see disc.c).
#   source /opt/toolchains/dc/kos/environ.sh && make
# hello0f_disc.cdi: the simulator's hello0f path (SCIF 1.5625 Mbaud), predicted 391 ms
# hello0_disc.cdi:  the simulator's hello0 path  (SCIF 115200 baud),  predicted 397 ms
# -N: no data-track padding (small images; padding only speeds up loading, which is outside the measured window).
# Burn a .cdi to a CD-R (DiscJuggler / cdrecord) or copy it to an optical drive emulator.
MKDCDISC = ../../mkdcdisc/build/mkdcdisc
TARGETS = hello0f_disc.cdi hello0_disc.cdi
all: $(TARGETS)
include $(KOS_BASE)/Makefile.rules
discf.o: disc.c
	$(KOS_CC) $(KOS_CFLAGS) -O2 -DFAST_SCIF -c $< -o $@
disc.o: disc.c
	$(KOS_CC) $(KOS_CFLAGS) -O2 -c $< -o $@
%.elf: %.o
	kos-cc -o $@ $^
hello0f_disc.cdi: discf.elf
	$(MKDCDISC) -q -N -e $< -n "SHRIKE4 INIT F" -a shrike4 --allow-overwrite -o $@
hello0_disc.cdi: disc.elf
	$(MKDCDISC) -q -N -e $< -n "SHRIKE4 INIT" -a shrike4 --allow-overwrite -o $@
clean:
	-rm -f *.o *.elf *.cdi
.PRECIOUS: %.o %.elf
