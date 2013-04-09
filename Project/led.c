/*********
 * led.c *
 *********
 * LED blink task
 */

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"

/* Project includes */
#include "lpc2103.h"
#include "led.h"

/* Project defines */
#define ledSTACK_SIZE ((unsigned portSHORT) configMINIMAL_STACK_SIZE)
#define slow_blink	1000
#define med_blink	500
#define fast_blink	100

/* Global variables */
static portLONG blink_delay;

/*****************
 * vStartLEDTask *
 *****************/
void vStartLEDTask( unsigned portBASE_TYPE uxPriority )
{
	xTaskCreate( vLEDTask, (const signed portCHAR*)"LED", ledSTACK_SIZE,( void * ) NULL, uxPriority,( xTaskHandle * ) NULL );
}

/************
 * LED Task *
 ************
 * Task to blink the LED on the Olimex LPC-P2103 development board
 */
void vLEDTask( void* pvParameters __attribute__ ((unused)))
{
	/* Task initialization code (runs once) */

	/* Default blink delay */
	blink_delay = fast_blink;

	/* Repetitive Task code (runs forever) */
	for(;;){

		/* Blink (toggle) the LED connected to the LPC2103 GPIO pin P0.26 */
		if READ(FIOPIN & 0x04000000) {
			WRITE(FIOCLR, 0x04000000);
		}
		else {
			WRITE(FIOSET, 0x04000000);
		}

		vTaskDelay((portTickType) blink_delay);
	}
}

/*************
 * End led.c *
 *************/
