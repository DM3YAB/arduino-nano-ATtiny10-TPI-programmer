#include <stdint.h>
#include <avr/io.h>

int main(void) {
  DDRB = 0b00000100;   // PB2 Ausgang

  while (1) {
    PINB = 0b00000100; // PB2 toggeln durch Schreiben auf PINB

    for (volatile unsigned long i = 0; i < 30000UL; i++) {
      asm volatile ("nop");
    }
  }
}