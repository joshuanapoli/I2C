/*********
 * cam.c *
 *********/

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"

/* Project includes */
#include "lpc2103.h"
#include "cam.h"
#include "i2c.h"

/* Project defines */
#define camSTACK_SIZE ((unsigned portSHORT) configMINIMAL_STACK_SIZE)

/* Global variables */

/* Queue variables for I2C */
xI2C_struct xCamI2C;
static xI2C_struct *pxCamI2C;

/*****************
 * vStartCAMTask *
 *****************/
void vStartCAMTask( unsigned portBASE_TYPE uxPriority )
{
	xTaskCreate( vCAMTask, (const signed portCHAR*)"CAM", camSTACK_SIZE,( void * ) NULL, uxPriority,( xTaskHandle * ) NULL );
}

/************
 * CAM Task *
 ************/
void vCAMTask( void* pvParameters __attribute__ ((unused)))
{
	/* Task initialization code (runs once) */

	/* Wait for the camera to initialize after power-up/reset */
	vTaskDelay((portTickType)1000);

	/* Repetitive Task code (runs forever) */
	for(;;){

#if 1
		ucI2C_Quick(pxCamI2C, 0x00, 0xFF);
		vTaskDelay((portTickType) 100);

		ucI2C_Quick(pxCamI2C, 0x01, 0xEE);
		vTaskDelay((portTickType) 100);

		ucI2C_SendByte(pxCamI2C, 0x02, 0xDD);
		vTaskDelay((portTickType) 100);

		ucI2C_ReceiveByte(pxCamI2C, 0x04);
		vTaskDelay((portTickType) 100);

		ucI2C_WriteByte (pxCamI2C, 0x08, 0xCC, 0xBB);
		vTaskDelay((portTickType) 100);

		ucI2C_ReadByte (pxCamI2C, 0x10, 0xAA);
		vTaskDelay((portTickType) 100);

		ucI2C_WriteWord (pxCamI2C, 0x20, 0x99, 0x88);
		vTaskDelay((portTickType) 100);

		ucI2C_ReadWord (pxCamI2C, 0x40, 0x77);
		vTaskDelay((portTickType) 100);

#endif
		vTaskDelay((portTickType) 1000);
	}
}


/***************
 * vCAM_Init() *
 ***************
 * CAM (camera) initialization code called by main
 */

void vCAM_Init( void )
{
	{
		portENTER_CRITICAL();

		/* Configure LPC-2103 P0.7 pin as Timer2 Match 0 (MAT2.0)
		 * - PINSEL[15:14] = 10
		 */
		WRITE(PINSEL0, (READ(PINSEL0) | 0x00008000));

		portEXIT_CRITICAL();

		/* Configure Timer2 to generate a camera pixel clock */

		/* Timer mode */
		WRITE(T2CTCR, 0x00);

		/* Prescale counter */
		WRITE(T2PC, 0x0000);

		/* Match control
		 * - reset on MR1
		 */
		WRITE(T2MCR, 0x0010);

		WRITE(T2MR0, 0x0003);
		WRITE(T2MR1, 0x0004);
		WRITE(PWM2CON, 0x00000001);
		WRITE(T2TCR, 0x01);

		/* I2C queue initialization
		 *
		 * Set the pxCamI2C pointer to the address of the xCamI2C structure. All
		 * I2C transaction parameters are passed via the xI2C structure. The
		 * pxCamI2CX pointer is passed to the i2cISR via the pxI2C_RQ queue. The
		 * i2cISR will use the pointer to access the xI2C structure created by
		 * the requesting task.
		 *
		 * Create the I2C completion queue for the CAM task and initialize the
		 * handle to the completion queue.
		 */
		pxCamI2C = &xCamI2C;
		pxCamI2C->pxHandle = (void *) xQueueCreate( (unsigned portBASE_TYPE) 1, (portBASE_TYPE) NULL );
		pxCamI2C->reqID = CAM_REQID;
	}

}
/*************
 * End cam.c *
 *************/
