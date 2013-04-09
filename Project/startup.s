/*************
 * startup.s *
 *************
 * NXP LPC2103 essential reset and initialziation assembler code
 * - initialization limited to items that MUST be done in assembler
 * - setup the interrupt vector table
 * - setup stack pointers
 * - branch to main.c
 	 - complete hardware initialization of items that CAN be done in C
 	 - run main application (written in C)
 */

/* NXP LPC2103 initialization steps:
 *
 * When reset is released the NXP LCP2103 boot loader is executed from ROM.
 *
 * - The boot loader image is stored in an 8 KB "boot block" at 0x7FFFE000.
 *
 * - When the boot loader is running the interrupt vector table (first 64
 *   bytes of the 32-bit address space: 0x00000000 to 0x00000003F) is mapped
 *   to the start of the boot block 0x7FFFE000 to 0xFFFE03F).
 *
 * - As a result the reset vector (0x00000000) is fetched from the boot block
 *   (0x7FFFE0000) which invokes the boot loader.
 *
 * The boot loader checks the reset state of the P0.14 pin (latched at reset).
 *
 * - IF P0.14 was asserted at reset the boot loader will continue to run and
 *   can be used to load a new FLASH image via UART0 (using FlashMagic).
 *
 *     http://www.flashmagictool.com/
 *
 * - If P0.14 was NOT asserted at reset the boot loader will compute the 2's
 *   compliment checksum of the interrupt vetor table contained in the FLASH
 *   (64-bytes at 0x00000000 to 0x0000003F). The 2's compliment checksum must
 *   match the checksum stored at 0x00000014 (computed and inserted by
 *   FlashMagic).
 *
 * - If the checksum is valid then the boot loader remaps the interrupt vector
 *   table to the FLASH image (0x00000000 to 0x0000003F) and the reset vector
 *   defined in this file (startup.s) is executed. This starts execution of
 *   the FLASH image.
 */

/* NXP LPC2103 RAM MAP
 *
 *   Size			= 0x00002000	(8 KBytes)
 *   RAM bottom	= 0x40000000
 *   RAM top		= 0x40001FFF
 *
 * NXP LPC2103 RAM usage
 *
 *	 0x40000000 - 0x4000001F	- RAM IRQ vectors						  32 bytes
 *	 0x40000020 - 0x4000003F	- RAM IRQ jump table					  32 bytes
 *	 0x40000040 - 0x4000011F	- Real Monitor							 224 bytes
 *	 0x40000120 - 0x400001FF	- In-System Programming (ISP) commands	 224 bytes
 *	 0x40000200 - 0x40001EDF	- RAM for general code usage			7392 bytes
 *	 0x40001EE0 - 0x40001FDF	- In-System Programming (ISP) stack		 256 bytes
 *	 0x40001FE0 - 0x40001FFF	- FLASH programming commands			  32 bytes
 */

/* External and global declarations */
	.extern	main
	.extern exit

	.extern	__bss_beg__
	.extern	__bss_end__
	.extern	__stack_end__
	.extern	__data_beg__
	.extern	__data_beg_src__

	.global	start
	.global endless_loop

/* ARM processor modes: usage
 *
 * - USER: 			Not used (USER is a non-privelaged mode)
 * - FIQ:			Fast interrupt (FIQ) service routine
 * - IRQ:			Interrupt (IRQ) service routine(s)
 * - SUPERVISOR:	Privelaged mode used by the FreeRTOS scheduler
 * - ABORT:			Data abort OR instruction prefetch abort exception
 * - SYSTEM:		Privelaged mode used by FreeRTOS tasks
 * - UNDEFINED:		Undefined instruction exception
 */
 	.set	UND_STACK_SIZE,	0x00000004
	.set	ABT_STACK_SIZE,	0x00000004
	.set	FIQ_STACK_SIZE,	0x00000080
	.set	IRQ_STACK_SIZE,	0x00000080
	.set	SVC_STACK_SIZE,	0x00000080

/* ARM CPU Mode bit defines - used in CPSR */
	.set	MODE_USR, 0x10
	.set	MODE_FIQ, 0x11
	.set	MODE_IRQ, 0x12
	.set	MODE_SVC, 0x13
	.set	MODE_ABT, 0x17
	.set	MODE_UND, 0x1B
	.set	MODE_SYS, 0x1F

/* CPSR interrupt mask bits
 * - I_BIT (bit 7)
 *		0 = IRQ disabled
 *		1 = IRQ enabled
 * - F_BIT (bit 6)
 *		0 = FIQ disabled
 *		1 = FIQ enabled
 */
	.set	I_BIT, 0x80
	.set	F_BIT, 0x40

/* Assembler directives to generate ARM code */
	.text
	.arm
	.align 0

/* Startup code - executes after reset
 *
 * - The boot loader (described above) will execute the reset vector
 *   located at 0x0000000 (see "B _start" instruction in section .startup
 *   below).
 *
 * - The reset vector causes a jump (branch) to the user "start" below
 *   (_start).
 *
 * - The start code initializes the stacks for each of the ARM CPU operating
 *   modes and jumps (branches) to "main.c".
 *
 * NOTE: After a reset, the ARM CPU is in supervisor mode and the IRQ_MASK and
 *       FIQ_MASK bits in the CPSR are set (interrupts are disabled).
 */
start:
_start:
_mainCRTStartup:

/* The __stack_end__ (.LC6) addresss is resolved by the linker.
 *
 * - see the "Literal Constant Pool" defined in this file
 */
	LDR		R0, .LC6

/* Initialize the stack for each ARM processor mode
 *
 * NOTE: User mode is NOT being used. System mode and User mode use the same
 *       registers and stack. However, System mode is a privelaged mode (can
 *       change CPSR mode bits) and User mode is not (cannot change CPSR mode
 *       bits).
 */
	MSR		CPSR_c, #MODE_UND|I_BIT|F_BIT
	MOV		SP, R0
	SUB		R0, R0, #UND_STACK_SIZE

	MSR		CPSR_c, #MODE_ABT|I_BIT|F_BIT
	MOV		SP, R0
	SUB		R0, R0, #ABT_STACK_SIZE

	MSR		CPSR_c, #MODE_FIQ|I_BIT|F_BIT
	MOV		SP, R0
	SUB		R0, R0, #FIQ_STACK_SIZE

	MSR		CPSR_c, #MODE_IRQ|I_BIT|F_BIT
	MOV		SP, R0
	SUB		R0, R0, #IRQ_STACK_SIZE

	MSR		CPSR_c, #MODE_SVC|I_BIT|F_BIT
	MOV		SP, R0
	SUB		R0, R0, #SVC_STACK_SIZE

	MSR		CPSR_c, #MODE_SYS|I_BIT|F_BIT
	MOV		SP, R0

/* ARM CPU must be in SUPERVISOR mode when code branches to main.c */
	MSR		CPSR_c, #MODE_SVC|I_BIT|F_BIT

/* Clear the .bss section
 *
 * - The .bss section contains (un-initialized) variables
 *
 * The __bss_beg__ (.LC1) and __bss_end__ (.LC2) addresses are resolved by
 * the linker.
 *
 * - see the "Literal Constant pool" defined in this file
 */
	LDR		R1, .LC1
	LDR		R3, .LC2
	SUBS	R3, R3, R1

	/* Test to see if the .bss section is empty */
	BEQ		end_clear_bss
	MOV		R2, #0

clear_bss_loop:
	STRB	R2,	[R1], #1
	SUBS	R3, R3, #1
	BGT		clear_bss_loop

end_clear_bss:

/* Copy the .data section from ROM to RAM. The .data section contains
 * (initialized) static variables.
 *
 * The __data_beg__ (.LC3), __data_beg_src__ (.LC4) and __data_end__ (.LC5)
 * addresses are resolved by the linker
 *
 * - see the "Literal Constant pool" defined in this file
 */
	LDR		R1, .LC3
	LDR		R2, .LC4
	LDR		R3, .LC5
	SUBS	R3, R3, R1

	/* Is the .data section empty? */
	BEQ		end_copy_data

copy_data:
	LDRB	R4, [R2], #1
	STRB	R4, [R1], #1
	SUBS	R3, R3, #1
	BGT		copy_data

end_copy_data:

/* ARM CPU register initialization before jumping to main.c
 *
 * ARM calling convention
 *	R0	- no argc to main.c
 *	R1	- no argv to main.c
 *	R7	- THUMB mode Frame Pointer
 *	R11	- ARM mode Frame Pointer
 */
	MOV		R0, #0
	MOV		R1, R0
	MOV		R7, R0
	MOV		R11, R0

/* Branch (jump) to main.c
 *
 * Initialization is completed in main.c
 */
	B	main

/* Endless loop - is this used anywhere? */
endless_loop:
	b	endless_loop

/* Literal Constant pool */
	.align 0

/* Start  and end of .bss section (in RAM)*/
	.LC1:
	.word	__bss_beg__

	.LC2:
	.word	__bss_end__

/* Start of .data section destination (in RAM) */
	.LC3:
	.word	__data_beg__

/* Start and end of .data section source (in ROM) */
	.LC4:
	.word	__data_beg_src__

	.LC5:
	.word	__data_end__

/* End of stack (RAM) */
	.LC6:
	.word	__stack_end__

/* Initialize default interrupt vector table (in ROM)
 *
 * - When the boot loarder prepares to execute user code from the FLASH it
 *   maps the interrupt vector table to the first 64 bytes of FLASH
 *
 *   - 0x00000000 to 0x0000003F
 *
 * - The first 64 bytes of the FLASH must be intialized with the vector table
 *   to be used when executing user code.
 *
 * - The boot loader boot loader will force execution of the code at the
 *   "reset vector" contained in the FLASH. This terminates execution of the
 *   boot loader and begins execution of the user code contained in the FLASH.
 *
 * NOTE: The boot loader uses address 0x00000014 (NOP below) to determine if
 *       the FLASH image containes valid code. FlashMagic computes a 2's
 *       complement checksum and stores it in this location when it loads the
 *       FLASH image.
 *
 * NOTE: The instruction "LDR PC, _name" loads the PC with the data contained
 *       in the address of symbol "_name". The data at "_name" specifies the
 *       address of the code that will execute when that exception condition
 *       occurs. The "interrupt vector jump table" appears below this code.
 */
 .section 	.startup, "ax"
 			.arm
 			.align 0

/* The ARM interrupt vectors and vector addresss are as follows:
 *
 * - 0x00000000 = Reset
 * - 0x00000004 = Undefined exception
 * - 0x00000008 = Software Interrupt
 * - 0x0000000C = Prefetch abort exception
 * - 0x00000010 = Data abort exception
 * - 0x00000014 = NOP (FlashMagic wites a 2's compliment checksum here)
 * - 0x00000018 = IRQ
 * - 0x0000001C = FIQ
 */
	B		_start
	LDR		PC,	_undf
	LDR		PC, _swi
	LDR		PC, _pabt
	LDR		PC, _dabt
	NOP

/* Load the PC with the interrupt vector address read from the NXP LPC2103
 * VICaddr register (0xFFFFF030).
 *
 * NOTE: The ARM7 CPU has a 3-stage pipeline (fetch, instruction decode,
 *       execute). As a result, the value in the PC is two instructions ahead
 *       of instruction execution (8 bytes becase this is 32-bit ARM code and
 *       each instruction is 4 bytes).
 *
 * NOTE: Look at the startup.lst file to see that the address of the
 *       "LDR PC, [PC, #-0xFF0]" instruction below is 0x00000018
 *
 * NOTE: (PC + 0x8) - 0xFF0 = 0x00000018 - 0x00000FF0 = 0xFFFFF030
 */
	LDR		PC, [PC, #-0xFF0]
	LDR		PC, _fiq

/* Interrupt vector jump table */
_undf:	.word	__undf
_swi:	.word	vPortYieldProcessor
_pabt:	.word	__pabt
_dabt:	.word	__dabt
_fiq:	.word	__fiq

/* Null-loops for unused interrupts
 *
 * NOTE: could branch to "endless_loop" but this approach gives an explicit
 *       endless loop for each exception type. This might be helpful in debug.
 */
__undf: b .
__pabt: b .
__dabt: b .
__fiq:  b .

/* End statup.s */
