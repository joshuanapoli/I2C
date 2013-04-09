
/*********
 * i2c.h *
 *********/
#ifndef I2C_H_
#define I2C_H_

/* I2C transaction opcode symbols. Opcodes match SMBus definitions. */
#define I2C_Quick			0x00	/* Single bit read/write */
#define I2C_SendByte		0x01	/* Byte write without a "command" byte */
#define I2C_ReceiveByte		0x02	/* Byte read without a "command" byte */
#define I2C_WriteByte		0x03	/* Byte write with a "command" byte */
#define I2C_ReadByte		0x04	/* Byte read with a "command" byte */
#define I2C_WriteWord		0x05	/* Word write with a "command" byte */
#define I2C_ReadWord		0x06	/* Word read with a "command" byte */

/* I2C transactions state symbols. These are used in the I2C ISR to track
 * the I2C transaction state.
 */
#define	I2C_STOP			0x00
#define I2C_START			0x01
#define I2C_RSTART			0x02
#define I2C_WR_ADDR 		0x10
#define I2C_WR_DATA			0x12
#define I2C_WR_COUNT		0x18
#define I2C_COMMAND			0x20
#define I2C_RD_ADDR 		0x40
#define I2C_RD_ADDR_ACK		0x41
#define I2C_RD_DATA_ACK 	0x42
#define I2C_RD_DATA_NAK		0x44
#define	I2C_RD_COUNT		0x48
#define I2C_LOST_ARB		0x80
#define I2C_ERROR_STOP		0xF0
#define I2C_ERROR			0xFF

/* I2C transaction parameter structure
 * - initialized by the requesting task including write data (if any)
 * - returns completion status and read data (if any)
 */
typedef struct xI2C_struct
{
	unsigned portCHAR reqID;		/* ID of requesting task */
	void * pxHandle;				/* Task-specific completion queue handle */
	unsigned portCHAR status;		/* I2C transaction completion status */
	unsigned portCHAR opcode;		/* I2C transaction opcode
										0: Quick Command
										1: Send Byte
										2: Receive Byte
										3: Write Byte
										4: Read Byte
										5: Write Word
										6: Read Word
									 */
	unsigned portCHAR addr;			/* Slave address of I2C device */
	unsigned portCHAR comm;			/* Command byte
										- Not used for Quick Command,
										  Send Byte, or Receive Byte
									 */
	unsigned portCHAR rd_len;		/* Number of read data bytes */
	unsigned portCHAR data[0x02];	/* Contains write data (for writes) or
									   read data (for reads)
									 */
} xI2C_struct;

/* Function Prototypes */
void vI2C_Init( unsigned portBASE_TYPE uxQueueLength );

unsigned portCHAR ucI2C_Quick (xI2C_struct *pxI2C,
							   unsigned portCHAR addr,
                               unsigned portCHAR data);

unsigned portCHAR ucI2C_SendByte (xI2C_struct *pxI2C,
		                          unsigned portCHAR addr,
                                  unsigned portCHAR data);

unsigned portCHAR ucI2C_ReceiveByte (xI2C_struct *pxI2C,
		                             unsigned portCHAR addr);

unsigned portCHAR ucI2C_WriteByte (xI2C_struct *pxI2C,
								   unsigned portCHAR addr,
                                   unsigned portCHAR cmd,
                                   unsigned portCHAR data);

unsigned portCHAR ucI2C_WriteWord (xI2C_struct *pxI2C,
								   unsigned portCHAR addr,
                                   unsigned portCHAR cmd,
                                   unsigned portBASE_TYPE data);

unsigned portCHAR ucI2C_ReadByte (xI2C_struct *pxI2C,
								  unsigned portCHAR addr,
                                  unsigned portCHAR cmd);

unsigned portCHAR ucI2C_ReadWord (xI2C_struct *pxI2C,
		                          unsigned portCHAR addr,
                                  unsigned portCHAR cmd);

#endif /*I2C_H_*/
