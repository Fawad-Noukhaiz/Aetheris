#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "../include/ipc.h"

static volatile int running = 1;
static void on_sig(int s) { (void)s; running = 0; }

int main(void) {
    signal(SIGINT, on_sig);

    int id = ipc_init("ChatClient");
    if (id < 0) {
        fprintf(stderr, "[ChatClient] Registration failed: %s\n",
                ipc_strerror(id));
        return 1;
    }
    printf("[ChatClient] Online (ID=%d).\n", id);

    int server_id = ipc_get_id_by_name("ChatServer");
    if (server_id < 0) {
        fprintf(stderr, "[ChatClient] ChatServer not found. "
                        "Start chat_server first.\n");
        ipc_shutdown();
        return 1;
    }
    printf("[ChatClient] Found ChatServer at ID %d.\n", server_id);
    printf("[ChatClient] Type a message and press Enter "
           "(Ctrl+C to quit):\n\n");

    char input[MAX_MSG_SIZE];
    while (running) {
        printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) break;

        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') input[--len] = '\0';
        if (len == 0) continue;

        ipc_message_t msg;
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.data, input, MAX_MSG_SIZE - 1);
        msg.size     = len;
        msg.priority = MSG_PRIORITY_NORMAL;

        int rc = ipc_send(server_id, &msg);
        if (rc != IPC_OK) {
            fprintf(stderr, "[ChatClient] Send failed: %s\n",
                    ipc_strerror(rc));
            continue;
        }

        ipc_message_t reply;
        memset(&reply, 0, sizeof(reply));
        rc = ipc_recv_timeout(&reply, 3000);
        if (rc == IPC_OK)
            printf("[ChatClient] Server: \"%s\"\n\n", reply.data);
        else
            fprintf(stderr, "[ChatClient] No reply: %s\n\n",
                    ipc_strerror(rc));
    }

    ipc_shutdown();
    printf("[ChatClient] Disconnected.\n");
    return 0;
}