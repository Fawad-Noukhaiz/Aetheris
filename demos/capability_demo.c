/*
 * capability_demo.c
 * Demonstrates Capability-Based Access Control.
 *
 * Three processes are created with different permission sets:
 *
 *   FullProcess   – CAP_ALL  (can send, recv, broadcast)
 *   SendOnly      – CAP_SEND only  (cannot recv or broadcast)
 *   RecvOnly      – CAP_RECV only  (cannot send or broadcast)
 *
 * The demo shows that attempting a forbidden operation returns
 * IPC_ERR_DENIED instead of succeeding.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "../include/ipc.h"

static void *send_only_thread(void *arg) {
    (void)arg;

    int id = ipc_init_with_caps("SendOnlyProc", CAP_SEND);
    if (id < 0) { fprintf(stderr, "SendOnly: reg failed\n"); return NULL; }

    printf("\n[SendOnly] Registered (ID=%d) with CAP_SEND only.\n", id);

    int full_id = ipc_get_id_by_name("FullProcess");
    if (full_id > 0) {
        ipc_message_t msg;
        memset(&msg, 0, sizeof(msg));
        strcpy(msg.data, "Hello from SendOnly!");
        msg.size     = strlen(msg.data);
        msg.priority = MSG_PRIORITY_NORMAL;

        int rc = ipc_send(full_id, &msg);
        printf("[SendOnly] ipc_send()      → %s  (expected: Success)\n",
               ipc_strerror(rc));
    }

    ipc_message_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    int rc = ipc_try_recv(&dummy);
    printf("[SendOnly] ipc_try_recv()  → %s  "
           "(expected: Permission denied)\n",
           ipc_strerror(rc));

    ipc_message_t bcast;
    memset(&bcast, 0, sizeof(bcast));
    strcpy(bcast.data, "Illegal broadcast attempt");
    bcast.size     = strlen(bcast.data);
    bcast.priority = MSG_PRIORITY_LOW;
    rc = ipc_broadcast(&bcast);
    printf("[SendOnly] ipc_broadcast() → %s  "
           "(expected: Permission denied)\n",
           ipc_strerror(rc));

    ipc_shutdown();
    return NULL;
}

static void *recv_only_thread(void *arg) {
    (void)arg;
    sleep(1);

    int id = ipc_init_with_caps("RecvOnlyProc", CAP_RECV);
    if (id < 0) { fprintf(stderr, "RecvOnly: reg failed\n"); return NULL; }

    printf("\n[RecvOnly] Registered (ID=%d) with CAP_RECV only.\n", id);

    int full_id = ipc_get_id_by_name("FullProcess");
    if (full_id > 0) {
        ipc_message_t msg;
        memset(&msg, 0, sizeof(msg));
        strcpy(msg.data, "Illegal send attempt");
        msg.size     = strlen(msg.data);
        msg.priority = MSG_PRIORITY_NORMAL;
        int rc = ipc_send(full_id, &msg);
        printf("[RecvOnly] ipc_send()      → %s  "
               "(expected: Permission denied)\n",
               ipc_strerror(rc));
    }

    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    int rc = ipc_recv_timeout(&msg, 3000);
    printf("[RecvOnly] ipc_recv_timeout() → %s  "
           "(expected: Success or Timeout)\n",
           ipc_strerror(rc));
    if (rc == IPC_OK)
        printf("[RecvOnly] Message content: \"%s\"\n", msg.data);

    ipc_shutdown();
    return NULL;
}

int main(void) {
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   Capability-Based Access Control Demo       ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    pthread_t t1, t2;
    pthread_create(&t1, NULL, send_only_thread, NULL);
    pthread_create(&t2, NULL, recv_only_thread, NULL);

    int id = ipc_init_with_caps("FullProcess", CAP_ALL);
    if (id < 0) {
        fprintf(stderr, "FullProcess: reg failed\n");
        return 1;
    }
    printf("[FullProcess] Registered (ID=%d) with CAP_ALL.\n", id);

    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    int rc = ipc_recv_timeout(&msg, 5000);
    if (rc == IPC_OK)
        printf("[FullProcess] Received: \"%s\" from ID %d\n\n",
               msg.data, msg.sender_id);
    else
        printf("[FullProcess] recv: %s\n\n", ipc_strerror(rc));

    int recv_id = ipc_get_id_by_name("RecvOnlyProc");
    if (recv_id > 0) {
        ipc_message_t out;
        memset(&out, 0, sizeof(out));
        strcpy(out.data, "Gift from FullProcess to RecvOnly");
        out.size     = strlen(out.data);
        out.priority = MSG_PRIORITY_HIGH;
        rc = ipc_send(recv_id, &out);
        printf("[FullProcess] Sent to RecvOnly → %s\n", ipc_strerror(rc));
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    ipc_shutdown();
    printf("\n[FullProcess] Capability demo complete.\n");
    return 0;
}