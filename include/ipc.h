#ifndef IPC_H
#define IPC_H

#include <stddef.h>

#define MAX_MSG_SIZE      1024
#define MAX_PROCESSES     64
#define MAX_QUEUE_SIZE    128
#define MAX_NAME_LEN      64

#define SERVER_SOCK_PATH  "/tmp/ipc_server.sock"
#define MONITOR_SOCK_PATH "/tmp/ipc_monitor.sock"

typedef enum {
    MSG_PRIORITY_LOW      = 0,
    MSG_PRIORITY_NORMAL   = 1,
    MSG_PRIORITY_HIGH     = 2,
    MSG_PRIORITY_CRITICAL = 3
} msg_priority_t;

typedef struct {
    int            msg_id;
    int            sender_id;
    int            receiver_id;
    msg_priority_t priority;
    size_t         size;
    char           data[MAX_MSG_SIZE];
    long long      timestamp_ms;
} ipc_message_t;

typedef enum {
    IPC_OK                =  0,
    IPC_ERR_INVALID_DST   = -1,
    IPC_ERR_QUEUE_FULL    = -2,
    IPC_ERR_TIMEOUT       = -3,
    IPC_ERR_NO_MSG        = -4,
    IPC_ERR_NOT_REG       = -5,
    IPC_ERR_SERVER_DOWN   = -6,
    IPC_ERR_GENERIC       = -7,
    IPC_ERR_DENIED        = -8
} ipc_error_t;

#define CAP_SEND      (1 << 0)
#define CAP_RECV      (1 << 1)
#define CAP_BROADCAST (1 << 2)
#define CAP_ALL       (CAP_SEND | CAP_RECV | CAP_BROADCAST)


int  ipc_init(const char *process_name);
int  ipc_init_with_caps(const char *process_name, unsigned int caps);
void ipc_shutdown(void);
int  ipc_send(int dest_id, ipc_message_t *msg);
int  ipc_recv(ipc_message_t *msg);

int  ipc_try_send(int dest_id, ipc_message_t *msg);
int  ipc_try_recv(ipc_message_t *msg);

int  ipc_recv_timeout(ipc_message_t *msg, int timeout_ms);

int  ipc_broadcast(ipc_message_t *msg);

int         ipc_get_id_by_name(const char *name);
int         ipc_get_my_id(void);
const char *ipc_strerror(int err);

#endif
