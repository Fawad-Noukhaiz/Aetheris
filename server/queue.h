#ifndef QUEUE_H
#define QUEUE_H

#include "../include/ipc.h"

void        queue_init(void);
ipc_error_t queue_enqueue(int dest_id, const ipc_message_t *msg);
ipc_error_t queue_dequeue(int dest_id, ipc_message_t *out);
ipc_error_t queue_try_dequeue(int dest_id, ipc_message_t *out);
ipc_error_t queue_dequeue_timeout(int dest_id, ipc_message_t *out,
                                   int timeout_ms);
int         queue_length(int dest_id);
void        queue_clear(int dest_id);

#endif