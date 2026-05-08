#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../include/ipc.h"

int main(void) {
    int id = ipc_init("PriorityCheck");
    if (id < 0) { fprintf(stderr, "ipc_init failed: %s\n", ipc_strerror(id)); return 1; }
    printf("Registered as ID=%d\n", id);

    struct { const char *label; msg_priority_t prio; } msgs[] = {
        {"LOW_1", MSG_PRIORITY_LOW},
        {"CRITICAL_1", MSG_PRIORITY_CRITICAL},
        {"NORMAL_1", MSG_PRIORITY_NORMAL},
        {"HIGH_1", MSG_PRIORITY_HIGH},
        {"CRITICAL_2", MSG_PRIORITY_CRITICAL},
        {"HIGH_2", MSG_PRIORITY_HIGH},
        {"LOW_2", MSG_PRIORITY_LOW}
    };
    int total = sizeof(msgs)/sizeof(msgs[0]);
    for (int i=0;i<total;i++) {
        ipc_message_t m;
        memset(&m,0,sizeof(m));
        strncpy(m.data, msgs[i].label, MAX_MSG_SIZE-1);
        m.size = strlen(m.data);
        m.priority = msgs[i].prio;
        m.receiver_id = id;
        int rc = ipc_send(id, &m);
        printf("send %s -> rc=%d (%s)\n", msgs[i].label, rc, ipc_strerror(rc));
    }

    const char *expected[] = {"CRITICAL_1","CRITICAL_2","HIGH_1","HIGH_2","NORMAL_1","LOW_1","LOW_2"};
    for (int i=0;i<total;i++) {
        ipc_message_t out; memset(&out,0,sizeof(out));
        int rc = ipc_recv_timeout(&out, 2000);
        printf("recv rc=%d (%s) data=""%s"" size=%zu\n", rc, ipc_strerror(rc), out.data, out.size);
    }

    ipc_shutdown();
    return 0;
}
