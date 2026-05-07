#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "../include/ipc.h"

static int g_receiver_id = -1;

typedef struct {
    const char    *name;
    msg_priority_t prio;
} sender_arg_t;

static void *sender_thread(void *arg) {
    sender_arg_t *sa = (sender_arg_t *)arg;

    int id = ipc_init(sa->name);
    if (id < 0) return NULL;

    const char *pnames[] = {"LOW","NORMAL","HIGH","CRITICAL"};
    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.priority = sa->prio;

    for (int i = 0; i < 5; i++) {
        snprintf(msg.data, MAX_MSG_SIZE,
                 "[%s] msg #%d  prio=%s",
                 sa->name, i + 1, pnames[sa->prio]);
        msg.size = strlen(msg.data);
        ipc_send(g_receiver_id, &msg);
    }
    ipc_shutdown();
    return NULL;
}

int main(void) {
    int recv_id = ipc_init("PriorityReceiver");
    if (recv_id < 0) {
        fprintf(stderr, "Failed to register receiver\n");
        return 1;
    }
    g_receiver_id = recv_id;
    printf("[PriorityDemo] Receiver ID = %d\n\n", recv_id);

    sender_arg_t args[] = {
        {"LowSender",      MSG_PRIORITY_LOW},
        {"NormalSender",   MSG_PRIORITY_NORMAL},
        {"HighSender",     MSG_PRIORITY_HIGH},
        {"CriticalSender", MSG_PRIORITY_CRITICAL}
    };
    pthread_t tids[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&tids[i], NULL, sender_thread, &args[i]);

    sleep(1);

    printf("[PriorityDemo] Receiving (CRITICAL should come first):\n\n");
    printf("  %-5s %-18s %s\n", "Order", "From-ID", "Message");
    printf("  %-5s %-18s %s\n", "-----", "-------", "-------");

    for (int i = 0; i < 20; i++) {
        ipc_message_t msg;
        memset(&msg, 0, sizeof(msg));
        int rc = ipc_recv_timeout(&msg, 3000);
        if (rc == IPC_OK)
            printf("  %-5d %-18d %s\n", i + 1, msg.sender_id, msg.data);
        else {
            printf("  [no more messages: %s]\n", ipc_strerror(rc));
            break;
        }
    }

    for (int i = 0; i < 4; i++) pthread_join(tids[i], NULL);
    ipc_shutdown();
    return 0;
}