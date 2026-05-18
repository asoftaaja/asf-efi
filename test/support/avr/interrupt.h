#pragma once
/* Mock AVR interrupt support.
 *
 * ISR(vector) expands to an ordinary function so tests can call ISR bodies
 * directly by name, e.g. TIMER1_CAPT_vect().
 *
 * cli/sei and SREG saves are no-ops; the SREG macro is provided by avr/io.h.
 */

#define ISR(vec) void vec(void)

#define cli()  do { } while (0)
#define sei()  do { } while (0)
