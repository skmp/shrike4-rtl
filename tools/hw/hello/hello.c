/* hello.c -- template for a hardware test: read some SH7091 registers, print them, return a status. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("hello: CCR %08lx MMUCR %08lx FRQCR %04x BCR1 %08lx BCR2 %04x WCR1 %08lx WCR2 %08lx MCR %08lx\n",
           (unsigned long)R32(0xFF00001C), (unsigned long)R32(0xFF000010), R16(0xFFC00000), (unsigned long)R32(0xFF800000),
           R16(0xFF800004), (unsigned long)R32(0xFF800008), (unsigned long)R32(0xFF80000C), (unsigned long)R32(0xFF800014));
    printf("hello: IPRA %04x IPRB %04x IPRC %04x TOCR %02x TSTR %02x TCR0 %04x\n", R16(0xFFD00004), R16(0xFFD00008),
           R16(0xFFD0000C), *(volatile uint8_t *)0xFFD80000, *(volatile uint8_t *)0xFFD80004, R16(0xFFD80010));
    return 0;
}
