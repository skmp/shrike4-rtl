# SH7091 cache / store-queue hardware tests: make (with the KOS environment: source /opt/toolchains/dc/kos/environ.sh),
# then ../hwrun.sh cachetest.elf (run 1) and ../hwrun.sh cachetest2.elf (run 2, follow-ups).
all: cachetest.elf cachetest2.elf cachetest3.elf
KOS_CFLAGS += -O2
include $(KOS_BASE)/Makefile.rules
clean:
	-rm -f *.o cachetest.elf cachetest2.elf cachetest3.elf
cachetest.elf: cachetest.o kern.o
	kos-cc -o $@ $^
cachetest2.elf: cachetest2.o kern.o
	kos-cc -o $@ $^
cachetest3.elf: cachetest3.o kern.o
	kos-cc -o $@ $^
