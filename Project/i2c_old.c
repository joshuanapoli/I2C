/*********
 * i2c.c *
 *********/

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Project includes */
#include "lpc2103.h"
#include "i2c.h"

#define i2cSTACK_SIZE	((unsigned portSHORT) configMINIMAL_STACK_SIZE)

/* Function prototypes */
extern void vI2C_ISRCreateQueue( unsigned portBASE_TYPE uxQueueLength);
void prvI2C_Transaction( xI2C_struct *pxI2C);

/* Declare glogal variables */
volatile unsigned portCHAR ucI2C_busy;

/* Declare external global variables */
extern xQueueHandle pxI2C_RQ;

/***************
 * vI2C_Init() *
 ***************
 * I2C initialization code called by main
 * - uxQueueLength specifies the number of entries in the request queue
 */
void vI2C_Init( unsigned portBASE_TYPE uxQueueLength )
{
	/* Declare enternal variables */
	extern void ( vI2C_ISR_Wrapper )(void);

	portENTER_CRITICAL();

	/* Configure the LPC-2103 pins used for I2C0
	 * - P0.3 = SDA0 (I2C0 data), PINSEL[7:6] = 01
	 * - P0.2 = SCL0 (I2C0 clock), PINSEL[5:4] = 01
	 */
	WRITE(PINSEL0, (READ(PINSEL0) | 0x50));

	/* Clear I2C0 register */
	WRITE(I2C0CONCLR, 0x7C);

	/* Configure the I2C0 clock for 100 KHz operation
	 * - 10 us period (5 us high, 5 us low)
	 * - CPU clock (Cclk) = 58.9824 MHz
	 * - Pclk = Cclk
	 * 		5 us ~= 295 / 58.9824 MHz
	 */
	WRITE(I2C0SCLH, 295);
	WRITE(I2C0SCLL, 295);

	/* Set I2C0 master enable */
	WRITE(I2C0CONSET, 0x40);

	/* Clear I2C0 interrupt (just in case) */
	WRITE(I2C0CONCLR, 0x8);

	/* Configure the Vectored Interrupt Controller for I2C1 interrupt
	 * - VIC channel 9 = I2C0 interrupt
	 * - Use VICVectAddr1
	 * - Use VICVectCntl1
	 * 		Set VIC IRQ "slot" enable, bit[5] = 1
	 * 		SET VIC IRQ channel = 9 (I2C0), bits[4:0] = 01001
	 *
	 * 		bits[5:0] = 0x29
	 */
	WRITE(VICVectAddr1, (unsigned portBASE_TYPE) vI2C_ISR_Wrapper);
	WRITE(VICVectCntl1, 0x29);

	/* Enable I2C0 interrupt (VIC channel 9)
	 * - Set VICIntEnable[9] (0x00000200)
	 */
	WRITE(VICIntEnable, (READ (VICIntEnable) | 0x00000200) );

	/* Initialize I2C busy flag = idle */
	ucI2C_busy = pdFALSE;

	portEXIT_CRITICAL();

	/* Create I2C request queue */
	vI2C_ISRCreateQueue( uxQueueLength );

} /* End of vI2C_Init */

/*****************
 * ucI2C_Quick() *
 ****************/
unsigned portCHAR ucI2C_Quick (xI2C_struct *pxI2C,
							   unsigned portCHAR addr,
							   unsigned portCHAR data)
{

	/* Initialize parameters for request */
	pxI2C->opcode	= I2C_Quick;		/* I2C transaction code */
	pxI2C->addr		= addr;		        /* Address (before left shift) */
	pxI2C->data[0]	= data;				/* Write data bit (low bit only) */

	/* Queue I2C transaction request and wait for completion */
	prvI2C_Transaction(pxI2C);

	/* I2C transaction complete, return status */
	return pxI2C->status;

} /*end ucI2C_Quick */

/********************
 * ucI2C_SendByte() *
 *******************/
unsigned portCHAR ucI2C_SendByte (xI2C_struct *pxI2C,
								  unsigned portCHAR addr,
								  unsigned portCHAR data)
{

	/* Initialize parameters for request */
	pxI2C->opcode	= I2C_SendByte;		/* I2C transaction code */
	pxI2C->addr		= addr;		        /* Address (before left shift) */
	pxI2C->data[0]	= data;				/* Write data byte */

	/* Queue I2C transaction request and wait for completion */
	prvI2C_Transaction(pxI2C);

	/* I2C transaction complete, return status */
	return pxI2C->status;

} /*end ucI2C_SendByte */

/***********************
 * ucI2C_ReceiveByte() *
 **********************/
unsigned portCHAR ucI2C_ReceiveByte (xI2C_struct *pxI2C,
									 unsigned portCHAR addr)
{
	/* Initialize parameters for request */
	pxI2C->opcode	= I2C_ReceiveByte;	/* I2C transaction code */
	pxI2C->addr		= addr;		        /* Address (before left shift) */

	/* Queue I2C transaction request and wait for completion */
	prvI2C_Transaction(pxI2C);

	/* I2C transaction complete, return status */
	return pxI2C->status;

} /*end ucI2C_ReceiveByte */

/*********************
 * ucI2C_WriteByte() *
 ********************/
unsigned portCHAR ucI2C_WriteByte (xI2C_struct *pxI2C,
								   unsigned portCHAR addr,
								   unsigned portCHAR cmd,
								   unsigned portCHAR data)
{

	/* Initialize parameters for request */
	pxI2C->opcode	= I2C_WriteByte;	/* I2C transaction code */
	pxI2C->addr		= addr;		        /* Address (before left shift) */
	pxI2C->comm		= cmd;		        /* Command byte (register offset) */
	pxI2C->data[0]	= data;				/* Write data byte */

	/* Queue I2C transaction request and wait for completion */
	prvI2C_Transaction(pxI2C);

	/* I2C transaction complete, return status */
	return pxI2C->status;

} /*end ucI2C_WriteByte */

/********************
 * ucI2C_ReadByte() *
 *******************/
unsigned portCHAR ucI2C_ReadByte (xI2C_struct *pxI2C,
								  unsigned portCHAR addr,
								  unsigned portCHAR cmd)
{
	/* Initialize parameters for request */
	pxI2C->opcode	= I2C_ReadByte;		/* I2C transaction code */
	pxI2C->addr		= addr;				/* Address (before left shift) */
	pxI2C->comm		= cmd;				/* Command byte (register offset */

	/* Queue I2C transaction request and wait for completion */
	prvI2C_Transaction(pxI2C);

	/* I2C transaction complete, return status */
	return pxI2C->status;

} /*end ucI2C_ReadByte */

/*********************
 * ucI2C_WriteWord() *
 ********************/
unsigned portCHAR ucI2C_WriteWord (xI2C_struct *pxI2C,
								   unsigned portCHAR addr,
								   unsigned portCHAR cmd,
								   unsigned portBASE_TYPE data)
{

	/* Initialize parameters for request */
	pxI2C->opcode	= I2C_WriteWord;	/* I2C transaction code */
	pxI2C->addr		= addr;		        /* Address (before left shift) */
	pxI2C->comm		= cmd;		        /* Command byte (register offset) */
										/* Write data, byte 0 and 1 */
	pxI2C->data[0]	= (unsigned portCHAR) (data & 0x00FF);
	pxI2C->data[1]	= (unsigned portCHAR)((data & 0xFF00) >> 8);

	/* Queue I2C transaction request and wait for completion */
	prvI2C_Transaction(pxI2C);

	/* I2C transaction complete, return status */
	return pxI2C->status;

} /*end ucI2C_WriteWord */

/********************
 * ucI2C_ReadWord() *
 ********************/
unsigned portCHAR ucI2C_ReadWord (xI2C_struct *pxI2C,
								  unsigned portCHAR addr,
								  unsigned portCHAR cmd)
{
	/* Initialize parameters for request */
	pxI2C->opcode	= I2C_ReadWord;			/* I2C transaction code */
	pxI2C->addr	= addr;					/* Address (before left shift) */
	pxI2C->comm	= cmd;					/* Command byte (register offset) */

	/* Queue I2C transaction request and wait for completion */
	prvI2C_Transaction(pxI2C);

	/* I2C transaction complete, return status */
	return pxI2C->status;

} /*end ucI2C_ReadWord */


/************************
 * prvI2C_Transaction() *
 ***********************/
void prvI2C_Transaction (xI2C_struct *pxI2C) {

	unsigned portCHAR q_status;

	/* Set status to error code
	 * - default status is error
	 * - transaction execution will modify the status
	 */
	pxI2C->status	= 0xFF;

	/* Queue the request */
	q_status = (portCHAR) xQueueSend ( pxI2C_RQ, (void *) &pxI2C, (portTickType) 0);

    if (q_status == pdTRUE) {
      /* Request successfully queued
  	   *
  	   * Check/modify the ucI2C_busy flag in a critical section
       * - If NOT busy
       * 	- kick start the I2C controller
       *    - set the ucI2C_busy flag
       *
       * NOTE: If an I2C transaction is already in progress then the
       *       new I2C transaction is simply put on the request queue.
       *       The I2CISR will complete the current I2C transaction and
       *       automatically begin servicing the next I2C request in the
       *       queue.
       */
      portENTER_CRITICAL();

      if (ucI2C_busy == pdFALSE) {
        /* Not busy... */
        WRITE(I2C0CONSET, 0x20);
         ucI2C_busy = pdTRUE;
      }

      portEXIT_CRITICAL();

      /* Wait for the I2C transaction to be completed...
       *
       * HACK HACK HACK - look into MAX limits
       */
      q_status = (portCHAR) xQueueReceive( pxI2C->pxHandle, NULL, (portTickType) 35);

      /* Check the response status */
      if( (q_status != (portCHAR) pdTRUE) || (pxI2C->status != 0) ) {

        /* I2C ERROR during the transaction...
         *
         * The I2CISR attempts to return the I2C controller to an operational
         * state. However, the transaction that encountered the error is NOT
         * recovered.
         *
         * Wait for the I2C bus to timeout (i.e. slave devices).
         *
         * HACK HACK HACK - The timeout value was chosen based on SMBus
         * requirements. This is NOT a guarantee that the bus will return
         * to an operational state. It may also be possible that a shorter
         * timeout would be okay.
         */
        vTaskDelay(35);

      } /* end if( (q_status != (portCHAR) pdTRUE) || (pxI2C->status != 0) ) */

    } /* end if (q_status == pdTRUE) */

    /* Transaction is done
     *
     * Status is in pxI2C structure
     * - if succesfull pxI2C->status == 0
     * - if NOT successful pxI2C->status != 0
     */
    return ;

} /*end prvI2C_Transaction */
