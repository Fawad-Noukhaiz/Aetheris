#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "../include/ipc.h"

static void *listener_thread(void *arg) {
    int   num = *(int *)arg;
    char  name[32];
    snprintf(name, sizeof(name), "Listener%d", num);

    int id = ipc_init(name);
    if (id < 0) {
        fprintf(stderr, "%s: registration failed\n", name);
        return NULL;
    }
    printf("[%s] Online (ID=%d).\n", name, id);

    for (int i = 0; i < 5; i++) {
        ipc_message_t msg;
        memset(&msg, 0, sizeof(msg));
        int rc = ipc_recv_timeout(&msg, 15000);
        if (rc == IPC_OK)
            printf("[%s] Received from ID %d: \"%s\"\n",
                   name, msg.sender_id, msg.data);
        else {
            printf("[%s] %s\n", name, ipc_strerror(rc));
            break;
        }
    }
    ipc_shutdown();
    return NULL;
}

int main(void) {
    int nums[3] = {1, 2, 3};
    pthread_t tids[3];
    for (int i = 0; i < 3; i++)
        pthread_create(&tids[i], NULL, listener_thread, &nums[i]);

    sleep(1);

    int id = ipc_init("Broadcaster");
    if (id < 0) { fprintf(stderr, "Broadcaster: reg failed\n"); return 1; }
    printf("\n[Broadcaster] Online (ID=%d). Sending 5 broadcasts...\n\n",
           id);

    for (int i = 1; i <= 5; i++) {
        ipc_message_t msg;
        memset(&msg, 0, sizeof(msg));
        snprintf(msg.data, MAX_MSG_SIZE,
                 "Broadcast #%d from Broadcaster", i);
        msg.size     = strlen(msg.data);
        msg.priority = MSG_PRIORITY_HIGH;

        int rc = ipc_broadcast(&msg);
        printf("[Broadcaster] Sent broadcast #%d  (%s)\n",
               i, ipc_strerror(rc));
        sleep(1);
    }

    for (int i = 0; i < 3; i++) pthread_join(tids[i], NULL);
    ipc_shutdown();
    printf("\n[Broadcaster] Done.\n");
    return 0;
}