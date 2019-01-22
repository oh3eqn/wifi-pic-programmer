/* Serial Programmer main module's header */

#ifndef _SP_H__
#define _SP_H__

#include <c_types.h>

#ifndef SP_VERBOSE
#define SP_VERBOSE	true
#endif

#define SP_VERSION	"0.1.0a"


typedef enum {
	SP_CMD_ECHO = 1
} SPCommand;


typedef struct {
	uint8_t command;
	uint32_t body_length;
} SPPacketHead;


typedef struct {
	SPPacketHead head;
	char *body;
} SPPacket;


typedef enum {
	SP_OK = 1,
	SP_ERR_INVALID_COMMAND,
	SP_ERR_REQ_LEN,
} SPError;

#endif