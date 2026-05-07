#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "../include/ipc.h"

static volatile int running = 1;
static void on_sig(int s) { (void)s; running = 0; }

int main(void) {
    signal(SIGINT, on_sig);

    int id = ipc_init("ChatServer");
    if (id < 0) {
        fprintf(stderr, "[ChatServer] Registration failed: %s\n",
                ipc_strerror(id));
        return 1;
    }
    printf("[ChatServer] Online (ID=%d). Waiting for messages...\n\n",
           id);

    while (running) {
        ipc_message_t msg;
        memset(&msg, 0, sizeof(msg));

        int rc = ipc_recv_timeout(&msg, 1000);
        if (rc == IPC_ERR_TIMEOUT) continue;
        if (rc != IPC_OK) {
            fprintf(stderr, "[ChatServer] recv error: %s\n",
                    ipc_strerror(rc));
            continue;
        }

        printf("[ChatServer] From ID %-3d | %s\n",
               msg.sender_id, msg.data);

        ipc_message_t reply;
        memset(&reply, 0, sizeof(reply));

        char truncated[MAX_MSG_SIZE - 6];
        strncpy(truncated, msg.data, sizeof(truncated) - 1);
        truncated[sizeof(truncated) - 1] = '\0';

        snprintf(reply.data, MAX_MSG_SIZE, "Echo: %s", truncated);
        reply.size     = strlen(reply.data);
        reply.priority = MSG_PRIORITY_NORMAL;

        rc = ipc_send(msg.sender_id, &reply);
        if (rc != IPC_OK)
            fprintf(stderr, "[ChatServer] send error: %s\n",
                    ipc_strerror(rc));
    }

    printf("[ChatServer] Shutting down.\n");
    ipc_shutdown();
    return 0;
}