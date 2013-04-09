/************
 * i2cISR.c *
 ************

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Project-specific includes */
#include "FreeRTOSConfig.h"
#include "lpc2103.h"
#include "i2c.h"

/* Declare external global variables */
extern xSemaphoreHandle xI2CSemaphore;

/* Function prototypes */
void vI2C_ISR_Wrapper(void) __attribute__ ((naked));
void vI2C_ISR(void);

/**********************
 * vI2C_ISR_Wrapper() *
 **********************/
/* The I2C ISR can cause a context switch so this "wrapper" does the
 * following things:
 *
 * 1) saves the context of the interrupted task
 * 2) calls vI2C_ISR() which contains the I2C specific ISR code
 * 		- the ISR implements the low-level interrupt service code
 * 		- the ISR may cause a higher priority task to become unblocked
 * 		and calls portYIELD_FROM_ISR() if it does
 * 		- the ISR completes and execution returns to this wrapper code
 * 3) restores the context of the highest priority task that can execute
 * 		- this might not be the same task that was interrupted
 */
void vI2C_ISR_Wrapper( void )
{
	/* Save the context of the interrupted task */
	portSAVE_CONTEXT();

	/* Call the real ISR code.
	 *
	 * NOTE: This must be a separate function from the wrapper to ensure
	 * the correct stack frame is set up.
	 */
	vI2C_ISR();

	/* Restore the context of the task that is going to run next */
	portRESTORE_CONTEXT();
}


/* I2C_ISR - I2C interrupt service routine */
void vI2C_ISR(void)
{
	/* Declare local variables */
	static portBASE_TYPE xI2CSemaphoreWokeTask;

	/* Initialize variables */
	xI2CSemaphoreWokeTask = pdFALSE;

	/* Verify that I2C0 is asserting an interrupt at the VIC
	 * - VICIRQStatus[9]
	 * 		- 0 = I2C0 is NOT asserting IRQ
	 * 		- 1 = I2C0 is asserting IRQ
	 */
	if(READ(VICIRQStatus) & 0x00000200){

		/* I2C0 interrupt is asserted.
		 *
		 * Give the semaphore to the I2C handler task.
		 */
		xSemaphoreGiveFromISR(xI2CSemaphore, &xI2CSemaphoreWokeTask);

		/* Mask the I2C0 interrupt at the VIC.
		 *
		 * NOTE: the I2C0 handler will service (clear) the I2C0 interrupt and
		 * re-enable the I2C0 interrupt at the VIC. In other words, the I2C0
		 * handler implements deferred interrupt processing for I2C0. This is
		 * done to minimize the time spent in the I2C ISR.
		 */
		WRITE(VICIntEnClear, 0x00000200);

		/* Upon return form ISR yield to a higher priority task if necessary */
		if (xI2CSemaphoreWokeTask == pdTRUE) {
			portYIELD_FROM_ISR();
		} /* end if (xI2CSemaphoreWokeTask == pdTrue) */
	}
	else {
		/* I2C0 interrupt is not asserted. Handle this condition as a
		 * spurious interrupt. Simply return from this IRS after resetting
		 * the VICVectAddr register (via a dummy write at the end of the ISR).
		 *
		 * NOTE: no code to execute here.
		 */
	} /* end if(READ(VICIRQStatus) & 0x00000010) */

	/* Reset Vectored Interrupt Controller priority encoder (VICVectAddr) by
	 * doing a dummy End-of-Interrupt write to VICVectAddr (required).
	 */
	WRITE(VICVectAddr, 0x0);

} /* End I2C_ISR */
