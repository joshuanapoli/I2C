/*********
 * cam.h *
 ********/
#ifndef CAM_H_
#define CAM_H_

/* Function prototypes */
void vStartCAMTask( unsigned portBASE_TYPE uxPriority );
void vCAMTask( void* pvParameters __attribute__ ((unused)));
void vCAM_Init( void );

/* Defines for CAM task */
/* Requestor IDs for shared queues */
#define CAM_REQID      0x1

#endif /* CAM_H_ */
