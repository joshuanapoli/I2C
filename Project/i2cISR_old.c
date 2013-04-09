/************
 * i2cISR.c *
 ************

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Project-specific includes */
#include "FreeRTOSConfig.h"
#include "lpc2103.h"
#include "i2c.h"

#define i2cSTACK_SIZE	((unsigned portSHORT) configMINIMAL_STACK_SIZE)

/* Function prototypes */
void vI2C_ISRCreateQueue(unsigned portBASE_TYPE uxQueueLength);
void vI2C_ISR_Wrapper(void) __attribute__ ((naked));
void vI2C_ISR(void);

/* Declare Global Variables */
xQueueHandle pxI2C_RQ;
extern volatile unsigned portCHAR ucI2C_busy;

/**************************
 * vI2C_ISRCreateQueue() *
 *************************/
void vI2C_ISRCreateQueue( unsigned portBASE_TYPE uxQueueLength )
{
	/* Create the I2C transaction request queue
	 * - one entry per task that issues I2C transaction requests
	 */
	pxI2C_RQ = xQueueCreate( uxQueueLength, sizeof( xI2C_struct * ) );
}

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
	static xI2C_struct *pxI2C;	/* Pointer to parameter structure passed via RQ */

	static unsigned portCHAR ucI2C_status;	/* Status read from I2C controller */
	static unsigned portCHAR ucI2C_saddr;	/* slave address bits[7:1]+ R/W bit[0] */
	static unsigned portCHAR ucI2C_lstate;	/* Last I2C transaction state */
	static unsigned portCHAR ucI2C_cstate;	/* Current I2C transaction state */
	static unsigned portCHAR ucI2C_wr_count;	/* Number of data bytes transmitted */
	static unsigned portCHAR ucI2C_rd_count;	/* Number of data bytes received */
	static unsigned portCHAR ucI2C_lostarb;	/* Lost arbitration flag */

	/* Pending transaction flag */
	static unsigned portCHAR ucI2C_pending = pdFALSE;
	static portBASE_TYPE xRequestWokeTask;
	static portBASE_TYPE xResponseWokeTask;
	static portBASE_TYPE xNewRequestWokeTask;

WRITE(FIOSET, 0x04000000);

	/* Initialize queue variables */
	xRequestWokeTask = pdFALSE;
	xResponseWokeTask = pdFALSE;
	xNewRequestWokeTask = pdFALSE;

	/* Verify that I2C0 is asserting an interrupt
	 *
	 * - I2C0CONSET bit 3: Interrupt Status
	 * 						0 - No IRQ from I2C0
	 * 						1 - I2C0 IRQ asserted
	 */
	if(READ(I2C0CONSET) & 0x08) {

		/* I2C0 interrupt is asserted, get current I2C status */
		ucI2C_status = READ(I2C0STAT);

		/* Save "last" state. Note that if this is the start of a new I2C
		 * transaction then this state will be initialized to "I2C_START"
		 * in case 0x8 of the switch statement below.
		 */
		ucI2C_lstate = ucI2C_cstate;

		/* One interrupt occurs for each phase of an I2C transaction (I2C0
		 * controller state transitions). A switch statement executes
		 * state-specific code that determines the next I2C0 controller
		 * action.
		 *
		 * After the case-specific code has been executed the current I2C
		 * interrupt is cleared and VIC priority is reset (by a dummy write
		 * to VICADDR).
		 */
		switch(ucI2C_status) {
			/* CASE 0x00 - Bus ERROR */
			case 0x00:
				/* Could only get here if there was an error
				 * - BUS ERROR is detected by hardware
				 *
				 * Set current I2C transaction state
				 */
				ucI2C_cstate = I2C_ERROR_STOP;

				/* The I2C transaction is terminated by asserting the STOP
				 * bit in I2C0CONSET is asserted at the end of this ISR.
				 */
				WRITE(I2C0CONSET, 0x10);

				break; /* case 0x00 */

			/* CASE 0x08 - START transmitted
			 *
			 * The requesting task must queue a request and then write 0x20 to
			 * I2C0CONSET. As a result of the write to I2C0CONSET the I2C
			 * controller will generate a START condition. An I2C interrupt
			 * will result with a status of 0x08.
			 *
			 * This state occurs at the beginning of ANY I2C transaction.
			 */
			case 0x08:
				/* Update firmware I2C transaction state variables. Note that
				 * in this case we KNOW that the last state was START.
				 *
				 * Assuming this is a write for now (if it is a read this will
				 * be modified later).
				 */
				ucI2C_lstate = I2C_START;
				ucI2C_cstate = I2C_WR_ADDR;

				/* Initialize ucI2C_lostarb (lost arbitration)
				 * - see case 0x38 and case 0x10
				 */
				ucI2C_lostarb = 0;

				/* Initialize data counters */
				ucI2C_wr_count = 0;
				ucI2C_rd_count = 0;

				/* Clear the start bit */
				WRITE(I2C0CONCLR, 0x20);

				/* An I2C transaction can be started in one of two ways...
				 *
				 * 1) ucI2C_pending == pdFALSE
				 *
				 *    The request queue was empty and a new I2C transaction
				 *    request is queued and started by I2C.c the request is
				 *    removed from the queue by the ISR following this comment.
				 *
				 * 2) ucI2C_pending == pdTRUE
				 *
				 *    A previous I2C transaction was completed (by this ISR)
				 *    and (one or more) I2C transaction requests were pending
				 *    at that time. (already queued). The next I2C transaction
				 *    was removed from the queue and the transaction was
				 *    started by code at the bottom of this ISR.
				 */
				if (ucI2C_pending == pdFALSE) {

					/* The transaction was started by I2C.c. Get the next
					 * request from the request queue. The "request" is the
					 * address of a pointer to a structure that contains the
					 * I2C transaction parameters/
					 */
					xQueueReceiveFromISR(pxI2C_RQ, &(pxI2C), &xRequestWokeTask);

				} /* end if (ucI2C_pending == pdFALSE) */

				/* Compose the I2C address from the slave address and the
				 * opcode. The 7-bit address is placed in the MSBs (bits 7:1)
				 * and the R/W bit is placed in bit 0.
				 *
				 * Assuming it is a write for now (if it is a read, it will be
				 * modified below)
				 */
				ucI2C_saddr = pxI2C->addr << 1;

				/* ucI2C_saddr[0] = 0 by default (as the result of the shift)
				 * which indicates a "write".
				 *
				 * If we have a RECEIVE BYTE opcode set bit 0 to indicate a
				 * "read".
				 *
				 * NOTE: Transactions that have a "command" byte indicate
				 * "read" in the slave address transmitted after the "repeated
				 * START" (and NOT here).
				 */
				if (pxI2C->opcode == 2){
					ucI2C_saddr = ucI2C_saddr | 0x01; /* Set bit 0 - "read" */
					ucI2C_cstate = I2C_RD_ADDR;
				}

				/* Load the slave address for transmission */
				WRITE(I2C0DAT, ucI2C_saddr);

				break; /* case 0x08 */

			/* CASE 0x10 - Repeated START transmitted
			 *
			 * Occurs in both Master Transmit and Master Receive modes
			 *
			 * Repeated start will occur in the following cases
			 * - loss of arbitration
			 * - transaction sequences requiring a repeated start
			 *   	- READ BYTE and READ WORD
			 */
			case 0x10:

				/* Set current I2C transaction state.
				 * Write the I2C slave address to I2C0.
				 */
				if (ucI2C_lostarb) {
					if (pxI2C->opcode == 2){
						ucI2C_cstate = I2C_RD_ADDR;
					}
					else{
						ucI2C_cstate = I2C_WR_ADDR;
					}

					/* Reset ucI2C_lostarb (lost arbitration) */
					ucI2C_lostarb = 0;
				}
				else {
					/* Got here because of a "repeated START" which occurs for the
					 * READ BYTE and READ WORD transaction
					 *
					 * NOTE: All of these transactions have a COMMAND byte.
					 */
					ucI2C_cstate = I2C_RD_ADDR;

					/* In this case, we need to set bit 0 in the slave address to
					 * indicate a read.
					 */
					ucI2C_saddr = ucI2C_saddr | 0x01;
				}

				/* Clear the START bit */
				WRITE(I2C0CONCLR, 0x20);
				/* Load the slave address for transmission */
				WRITE(I2C0DAT, ucI2C_saddr);


				break; /* case 0x10 */

			/* CASE 0x18 -  Slave Address+Write transmitted ACK received
			 *
			 * Occurs in Master Transmit mode only
			 * Previous state was 0x08 or 0x10
			 */
			case 0x18:

				switch( pxI2C->opcode){

					case 0:	/* QUICK COMMAND */
						/* Set current I2C transaction state */
						ucI2C_cstate = I2C_STOP;

						/* Transaction complete.
						 *
						 * The I2C transaction is terminated by asserting the STOP
						 * bit in I2C0CONSET is asserted at the end of this ISR.
						 */

						break; /* case 0 QUICK COMMAND */

					case 1: /* SEND BYTE */
						/* Set current I2C transaction state */
						ucI2C_cstate = I2C_WR_DATA;

						/* Transmit data byte 0 */
						WRITE(I2C0DAT, pxI2C->data[ucI2C_wr_count]);
						ucI2C_wr_count++;

						break; /* case 1 SEND BYTE */

					case 3: /* WRITE BYTE */
					case 4: /* READ BYTE */
					case 5: /* WRITE WORD */
					case 6: /* READ WORD */

						/* Set current I2C transaction state */
						ucI2C_cstate = I2C_COMMAND;

						/* Transmit command byte */
						WRITE(I2C0DAT, pxI2C->comm);

						break; /* case 3 - 9 */

					default:

						/* Could only get here if an error occured
						 *
						 * Set current I2C transaction state
						 */
						ucI2C_cstate = I2C_ERROR_STOP;

						/* The I2C transaction is terminated by asserting the STOP
						 * bit in I2C0CONSET is asserted at the end of this ISR.
						 */

				} /* End switch( pxI2C->opcode) */

				break; /* case 0x18 */

			/* CASE 0x20 - Slave Address+Write transmitted NACK received
			 *
			 * Occurs in Master Transmit mode only
			 * Previous state was 0x08 or 0x10
			 */
			case 0x20:
				/* Slave NACK'd the transaction - this is an error.
				 *
				 * Set current I2C transaction state
				 */
				ucI2C_cstate = I2C_ERROR_STOP;

				/* The I2C transaction is terminated by asserting the STOP
				 * bit in I2C0CONSET is asserted at the end of this ISR.
				 */

				break; /* case 0x20 */

			/* CASE 0x28 - Data transmitted ACK received
			 *
			 * Occurs in Master Transmit mode only
			 * Previous state was 0x18
			 */
			case 0x28:

				switch( pxI2C->opcode){

					case 1: /* SEND BYTE */
						/* Set current I2C transaction state */
						ucI2C_cstate = I2C_STOP;

						/* Transaction done.
						 *
						 * The I2C transaction is terminated by asserting the STOP
						 * bit in I2C0CONSET is asserted at the end of this ISR.
						 */

						break; /* case 1 SEND BYTE */

					case 3: /* WRITE BYTE */
						if(ucI2C_wr_count == 0){
							/* Send data byte
							 *
							 * Set current I2C transaction state
							 */
							ucI2C_cstate = I2C_WR_DATA;

							/* Transmit data byte */
							WRITE(I2C0DAT, pxI2C->data[ucI2C_wr_count]);
							ucI2C_wr_count++;
						}
						else{	/* Get here if data byte already sent */
							 /* All data sent, terminate transaction
							 *
							 * Set current I2C transaction state
							 */
							ucI2C_cstate = I2C_STOP;

							/* Transaction done.
							 *
							 * The I2C transaction is terminated by asserting the STOP
							 * bit in I2C0CONSET is asserted at the end of this ISR.
							 */

						}
						break;	/* Case 3 WRITE BYTE */

					case 5: /* WRITE WORD */
						if(ucI2C_wr_count <= 1){
							/* Send data byte
							 *
							 * Set current I2C transaction state
							 */
							ucI2C_cstate = I2C_WR_DATA;

							/* Transmit data byte */
							WRITE(I2C0DAT, pxI2C->data[ucI2C_wr_count]);
							ucI2C_wr_count++;
						}
						else{	/* Get here if data byte already sent */
							 /* All data sent, terminate transaction
							 *
							 * Set current I2C transaction state
							 */
							ucI2C_cstate = I2C_STOP;

							/* Transaction done.
							 *
							 * The I2C transaction is terminated by asserting the STOP
							 * bit in I2C0CONSET is asserted at the end of this ISR.
							 */

						}

						break; /* case 5 WRITE WORD */

					case 4: /* READ BYTE */

					case 6: /* READ WORD */
						/* Set current I2C transaction state */
						ucI2C_cstate = I2C_RSTART;

						/* Transmit a REPEATED START */
						WRITE(I2C0CONSET, 0x20);

						break;  /* case READ BYTE, READ WORD, READ BLOCK */

					default:
						/* Could only get here if an error occurred
						 *
						 * Set current I2C transaction state
						 */
						ucI2C_cstate = I2C_ERROR_STOP;

						/* The I2C transaction is terminated by asserting the STOP
						 * bit in I2C0CONSET is asserted at the end of this ISR.
						 */

				} /* End switch( pxI2C->opcode) */

				break; /* case 0x28 */

			/* CASE 0x30 - Data transmitted NACK received
			 *
			 * Occurs in Master Transmit mode only
			 * Previous state was 0x18
			 */
			case 0x30:
				/* Slave NACK'd transaction - this an error
				 *
				 * Set current I2C transaction state
				 */
				ucI2C_cstate = I2C_ERROR_STOP;

				/* The I2C transaction is terminated by asserting the STOP
				 * bit in I2C0CONSET is asserted at the end of this ISR.
				 */

				break; /* case 0x30 */

			/* CASE 0x38 - Arbitration lost
			 *
			 * Occurs in Master Transmit or Master Receive mode
			 */
			case 0x38:
				/* This case restarts an LPC2103 transaction that lost
				 * arbitration.
				 * 	- NOTE: When the LPC2103 is the ONLY master on an I2C bus
				 *    then this case cannot happen.
				 *
				 * Set current I2C transaction state
				 */
				ucI2C_cstate = I2C_LOST_ARB;

				/* When arbitration is lost then another START condition must
				 * be transmitted.
				 */
				WRITE(I2C0CONSET, 0x20);
 				ucI2C_lostarb = 0x01;

				break; /* case 0x38 */

			/* CASE 0x40 - Slave Address+Read transmitted ACK received
			 *
			 * Occurs in Master Receive mode only
			 * Previous state was 0x08 or 0x10
			 */
 			case 0x40:

				switch(pxI2C->opcode){

					case 2:	/* RECEIVE BYTE */
					case 4: /* READ BYTE */
					case 6: /* READ WORD */
						/* Transition to Master-Reveiver mode
						 * - Slave transmits data
						 * - Master receives data, transmits ACK or NAK
						 *
						 * Set current I2C transaction state
						 */
						ucI2C_cstate = I2C_RD_ADDR_ACK;

						if( (pxI2C->opcode == 2) || (pxI2C->opcode == 4) ) {
						 	/* RECEIVE BYTE or READ BYTE
						 	 *
						 	 * Disable ACK
						 	 * - the first read data byte will also be the last
						 	 */
							WRITE(I2C0CONCLR, 0x04);
						}
						else { /* READ WORD, PROCESS CALL or READ BLOCK */
							/* Enable ACK
							 * - there will be more than one read data byte
							 */
							WRITE(I2C0CONSET, 0x04);
						}

						break; /* case 2, 4, 6, 7,  RECEIVE BYTE, READ BYTE, READ WORD, PROCESS CALL, READ BLOCK */

					default:
						/* Could only get here if an error occured
						 *
						 * Set current I2C transaction state
						 */
						ucI2C_cstate = I2C_ERROR_STOP;

						/* The I2C transaction is terminated by asserting the STOP
						 * bit in I2C0CONSET is asserted at the end of this ISR.
						 */

				} /* End switch(pxI2C->opcode) */

				break; /* case 0x40 */

			/* CASE 0x48 - Slave Address+Read transmitted NACK received
			 *
			 * Occurs in Master Receive mode only
			 * Previous state was 0x08 or 0x10
			 */
			case 0x48:
				/* Slave NACK'd transaction - this is an error
				 *
				 * Set current I2C transaction state
				 */
				ucI2C_cstate = I2C_ERROR_STOP;

				/* The I2C transaction is terminated by asserting the STOP
				 * bit in I2C0CONSET is asserted at the end of this ISR.
				 */

				break; /* case 0x48 */

			/* CASE 0x50 - Data byte received ACK transmitted
			 *
			 * Occurs in Master Receive mode only
			 */
			case 0x50:
				switch(pxI2C->opcode){

					case 6: /* READ WORD */
						/* Read data has been received and ACK has been transmitted
						 * - the first byte of read data for READ WORD or PROCESS CALL
						 *
						 * Set current I2C transaction state
						 */
						ucI2C_cstate = I2C_RD_DATA_NAK;

						/* Disable ACK ( don't acknowledge next (last) byte) */
						WRITE(I2C0CONCLR, 0x04);

						/* Read data byte */
						pxI2C->data[ucI2C_rd_count] = READ(I2C0DAT);
						ucI2C_rd_count++;

						break; /* case 6,7 READ WORD, PROCESS CALL */

					default:
						/* Could only get here if there was an error
						 *
						 * Set current I2C transaction state
						 */
						ucI2C_cstate = I2C_ERROR_STOP;

						/* The I2C transaction is terminated by asserting the STOP
						 * bit in I2C0CONSET is asserted at the end of this ISR.
						 */

				} /* switch(pxI2C->opcode) */

				break; /* case 0x50 */

			/* CASE 0x58 - Data byte received NACK transmitted
			 *
			 * Occurs in Master Receive mode only
			 */
			case 0x58:
				/* RECEIVE BYTE, PROCESS CALL, READ BYTE, READ WORD or READ BLOCK
				 * - Transaction done
				 *
				 * Set current I2C transaction state
				 */
				ucI2C_cstate = I2C_STOP;

				/* Read last data byte */
				pxI2C->data[ucI2C_rd_count] = READ(I2C0DAT);
				ucI2C_rd_count++;

				/* Transaction done.
				 *
				 * The I2C transaction is terminated by asserting the STOP
				 * bit in I2C0CONSET is asserted at the end of this ISR.
				 */

				break;

			/* CASE DEFAULT - If code execution gets here then there is an
			 * error.
			 */

			default:
				/* Could only get here if there was an error
				 *
				 * Set current I2C transaction state
				 */
				ucI2C_cstate = I2C_ERROR_STOP;

				/* The I2C transaction is terminated by asserting the STOP
				 * bit in I2C0CONSET is asserted at the end of this ISR.
				 */

		} /* End switch(ucI2C_status) */

		/* If we are done with the transaction then generate a response */
			if( (ucI2C_cstate == I2C_STOP) || (ucI2C_cstate == I2C_ERROR_STOP) ) {

				/* If I2C_STOP...
				 *
				 * The STOP bit in I2C0CONSET must be set to terminate
				 * (normally) the I2C transaction.
				 *
				 * If I2C_ERROR_STOP...
				 *
				 * Writing the STOP bit in the pxI2CCONET aborts a current
				 * I2C transaction and restores the I2C controller to an
				 * operational state. HOWEVER... this does not "recover" any
				 * I2C transaction that may have been in progress.
				 */
				WRITE(I2C0CONSET, 0x10);

				/* Return status, read length (count), and read data
				 * - pxI2C->rd_len and pxI2C->data[] already contain data
				 * - need to update pxI2C->status here
				 */
				pxI2C->status = ucI2C_cstate;


				/* Return the completion for the request */
				xQueueSendFromISR(pxI2C->pxHandle, (void *) NULL, &xResponseWokeTask);

				/* Done with the prior I2C transcation. Check for a pending
				 * transaction.
				 *
				 * An I2C transaction can be started in one of two ways...
				 *
				 * 1) The request and lock queues were empty and a new I2C
				 *    transaction request is queued and started by I2C.c the
				 *    request is removed from the queue at the top of this
				 *    ISR ->>> see switch(ucI2C_status) case 0x08
				 *
				 * 2) An I2C transaction was completed (by this ISR) and one or
				 *    more I2C transactions were pending (already queued). In
				 *    this case the next I2C transaction is started by the ISR
				 *    and the request is removed from the queue here ...
				 */
				ucI2C_pending = (unsigned portCHAR) xQueueReceiveFromISR(pxI2C_RQ, &(pxI2C), &xNewRequestWokeTask);

				if (ucI2C_pending == pdTRUE) {

					/* There is a pending transaction.
					 *
					 * Start the new I2C transacton. This will case an
					 * interrupt for the new transaction.
					 *
					 * NOTE: ucI2C_busy will remain == pdTRUE
					 */

					/* Start I2C port 0 */
					WRITE(I2C0CONSET, 0x20);
				}
				else {
					/* No pending I2C transactions. Clear the ucI2C_busy flag. */
					ucI2C_busy = pdFALSE;
				} /* end if (ucI2C_pending == pdTRUE) */

			} /* end if( (ucI2C_cstate == I2C_STOP) || (ucI2C_cstate == I2C_ERROR_STOP) ) */

		/* Interrupt serviced (for any cases above). Clear the I2C interrupt. */
		WRITE(I2C0CONCLR, 0x08);
	}
	else {
		/* If execution gets HERE (this else clause) then the VIC thought
		 * that an I2C controller asserted an interrupt but I2C status indicates
		 * that there is no interrupt pending.
		 *
		 * For now... assuming that this is a "spurious" VIC interrupt. There
		 * is nothing to do with I2C. However, when execution falls through
		 * this if/else clause the VIC will be read to reset the VIC priority
		 * as required.
		 */
	} /* if(READ(I2C0CONSET) & 0x08) */

	if( xResponseWokeTask | xResponseWokeTask | xNewRequestWokeTask )
	{
		portYIELD_FROM_ISR();
	}

	/* Reset Vectored Interrupt Controller priority encoder (VICVectAddr) */
	WRITE(VICVectAddr, 0x0);	//Required dummy End-of-Interrupt write
WRITE(FIOCLR, 0x04000000);

} /* End I2C_ISR */
