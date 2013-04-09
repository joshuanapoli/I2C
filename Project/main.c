/**********
 * main.c *
 **********/

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"

/* Project specific includes */
#include "led.h"
#include "i2c.h"

/* GPIO pin initialization for the NXP LPC2103
 *
 * INPUT - set bit = 0
 * OUTPUT - set bit = 1
 *
 * GPIO pins configured as outputs
 * 	P0.26 	= LED
 *  P0.25	= Software controlled GPIO pin for debug
 * 	P0.8	= UART1 Tx
 *
 * NOTE: All other pins are inputs (or not used)
 *
 * 3322 2222 2222 1111 1111 1100 0000 0000
 * 1098 7654 3210 9876 5432 1098 7654 3210
 * ++++-++++-++++-++++-++++-++++-++++-++++
 * 0000.0110.0000.0000.0000.0001.0000.0000 = 0x06000100
 */
#define mainGPIO_DIR ( (unsigned portLONG) 0x06000100 )

/* Define clock parameters to configure NXP LPC2103 PLL
 *
 * Fosc		= 14.7456 MHz (crystal on Olimex LPC-P2103 board)
 *
 * Cclk		= 4 * Fosc (desired clock multiplier * crystal frequency)
 * 			= 58.9834 MHz (desired Cclk frequency)
 *
 * NXP LPC2103 PLL limits: 156 MHz < Fcco < 320 MHz
 *
 * Fcco		= 2 * P * M * Fosc
* 			= 16 * Fosc	(P = 2, M = 4)
 * 			= 235.9296 MHz (satisfies PLL limits)
 *
 * Desired PLL configuration parameters
 *  PLLCFG[4:0] = M - 1 = 3 = 00011b
 *  PLLCFG[6:5] = Pselect (for P = 2, Pselect = 01b)
 *  PLLCFG[7] = 0 (reserved)
 *
 *  PLLCFG[7:0] = 00100011 = 0x23
 *
 * Peripheral clock (Pclk) is derived from the CPU clock (Cclk)
 *
 * NOTE: NXP LPC2103 APBDIV register defaults to 0x0 after reset
 * NOTE: Pclk = Cclk/4 by default
 *
 * Select Pclk = Cclk
 *   APBDIV = mainBUS_CLK_FULL = 0x01
 *
 * Define clock configuration parameters
 */
#define mainPLL_MUL_4		( ( unsigned portCHAR ) 0x0003 )
#define mainPLL_DIV_2		( ( unsigned portCHAR ) 0x0020 )
#define mainPLL_ENABLE		( ( unsigned portCHAR ) 0x0001 )
#define mainPLL_CONNECT		( ( unsigned portCHAR ) 0x0003 )
#define mainPLL_FEED_BYTE1	( ( unsigned portCHAR ) 0xAA )
#define mainPLL_FEED_BYTE2	( ( unsigned portCHAR ) 0x55 )
#define mainPLL_LOCK		( ( unsigned portLONG ) 0x0400 )
#define mainBUS_CLK_FULL	( ( unsigned portCHAR ) 0x01 )

/* FLASH Memory Accelerator Module (MAM) initialization
 *
 * Enable FLASH Memory Accelerator Module
 * - 4 Cclk cycles per MAM cycle
 * - MAM fully enabled
 */
#define mainMAM_TIM_4		( ( unsigned portCHAR ) 0x03 )
#define mainMAM_MODE_FULL	( ( unsigned portCHAR ) 0x02 )

/* Define task priorites */
#define mainI2C_TASK_PRIORITY		( tskIDLE_PRIORITY + 3 )
#define mainLED_TASK_PRIORITY		( tskIDLE_PRIORITY + 1 )
#define mainCAM_TASK_PRIORITY		( tskIDLE_PRIORITY + 2 )

/* Function prototypes */
static void prvSetupHardware( void );

/********
 * MAIN *
 ********
 * Main completes hardware initialization, starts the FreeRTOS tasks,
 * and starts the FreeRTOS scheduler.
 *
 * NOTE: The CPU is in supervisor mode when main is called from startup.s
 * NOTE: All initialization that MUST be done in assembler is done is startup.s
 * NOTE: All initialization that CAN be done in C is done in main.c (here)
 */
int main( void )
{
	/* Complete hardware initialization that is NOT task specific
	 *
	 * NOTE: Task-specific initialization is done during task
	 *       initialization (when the task is started)
	 */
	prvSetupHardware();

	/* Initialize I2C0
	 * - parameter specifies the number of queue entries
	 */
	vI2C_Init((unsigned portBASE_TYPE) 0x1);

	/* Initialize CAM (the camera) */
	vCAM_Init();

	/* Start tasks
	 *
	 * NOTE: Tasks run in SYSTEM mode
	 */
	vStartI2CTask ( mainI2C_TASK_PRIORITY );
	vStartLEDTask ( mainLED_TASK_PRIORITY );
	vStartCAMTask ( mainCAM_TASK_PRIORITY );

	/* Start the FreeRTOS scheduler
	 *
	 * NOTE: The scheduler runs in SUPERVISOR mode
	 */
	vTaskStartScheduler();

	/* The RTOS should run forever
	 *
	 * NOTE: This return statement should never be executed
	 */
	return 0;
}

/**********************
 * prvSetupHardware() *
 **********************
 * Complete hardware initialization that is NOT task specific
 *
 * Inputs: none
 * Outputs: none
 */
static void prvSetupHardware( void )
{
	/* Select FAST mode for the LPC2103 GPIO pins
	 * - bit 0 = 1
	 */
	WRITE(SCS, 0x0001);

	/* Configure LPC2103 GPIO pins (inputs vs. outputs) */
	WRITE(FIODIR, mainGPIO_DIR);

	/* Enable the FLASH Memory Accelerator Module (MAM)
	 *
	 * MAMTIM	- 4 Cclk cycles per MAM cycle
	 * MAMCR	- MAM fully enabled
	 */
	WRITE(MAMTIM, 0x04);
	WRITE(MAMCR, 0x02);

	/* Configure the LPC2103's PLL
	 *
	 * Fosc	= 14.7456 MHz
	 * PLL multiply = 4
	 * Cclk = 58.9824 MHz
	 */
	WRITE(APBDIV, 0x01);
	WRITE(PLLCFG, 0x23);

	/* PLL enable sequence */
	WRITE(PLLFEED, 0xAA);
	WRITE(PLLFEED, 0x55);

	/* Enable the PLL */
	WRITE(PLLCON, 0x01);

	/* PLL enable sequence */
	WRITE(PLLFEED, 0xAA);
	WRITE(PLLFEED, 0x55);

	/* Wait for PLL lock */
	while(!(READ(PLLSTAT) & 0x400));

	/* Select the PLL output as the source of Cclk */
	WRITE(PLLCON, 0x03);

	/* PLL enable sequence */
	WRITE(PLLFEED, 0xAA);
	WRITE(PLLFEED, 0x55);

	/* The LPC2103 should now be running at 58.9824 MHz */
}
