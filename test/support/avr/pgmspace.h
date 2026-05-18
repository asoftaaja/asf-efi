#pragma once
/* Mock AVR program-space (flash) string utilities.
 * On the host every string lives in RAM, so these are all pass-throughs.
 */
#define PROGMEM
#define PSTR(x)            (x)
#define pgm_read_byte(p)   (*((const uint8_t *)(p)))
#define pgm_read_word(p)   (*((const uint16_t *)(p)))
