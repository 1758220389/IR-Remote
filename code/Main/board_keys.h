#ifndef BOARD_KEYS_H
#define BOARD_KEYS_H
#include <stdint.h>
/* Outputs PC13, PA0, PA3, PA6, PB1 remain low. Inputs PC15, PA2,
 * PA5, PB0, PB11 have pull-ups. Never short these outputs to a supply.
 * Raw pressed bits 0..4 correspond to BUTTON1..5. No delay/debounce.
 */
void BoardKeys_Init(void);
uint8_t BoardKeys_ReadRaw(void);
#endif
