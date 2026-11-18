#ifndef GESTURE_SHADOW_H
#define GESTURE_SHADOW_H
#include <stdint.h>
void GestureShadow_Init(void);
void GestureShadow_Tick(uint32_t now);
/* Returns a key only on the call that completes an eligible valid window. */
uint8_t GestureShadow_Poll(uint32_t now, unsigned allow_proposal);
#endif
