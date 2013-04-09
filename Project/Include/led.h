/*********
 * led.h *
 *********
 * Include file for led.c
 */
#ifndef LED_H
#define LED_H

/* Function prototypes */
void vStartLEDTask( unsigned portBASE_TYPE uxPriority );
void vLEDTask( void* pvParameters __attribute__ ((unused)));

#endif /* LED_H */
