#include "queue.h"
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

typedef struct {
    ipc_message_t   msgs[MAX_QUEUE_SIZE];
    int             count;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
} msg_queue_t;

static msg_queue_t queues[MAX_PROCESSES];


static void make_timespec(struct timespec *ts, int ms) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static void insert_sorted(msg_queue_t *q, const ipc_message_t *msg) {
    int pos = q->count;
    while (pos > 0 && q->msgs[pos - 1].priority < msg->priority) {
        q->msgs[pos] = q->msgs[pos - 1];
        pos--;
    }
    q->msgs[pos] = *msg;
    q->count++;
}

void queue_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        memset(&queues[i], 0, sizeof(msg_queue_t));
        pthread_mutex_init(&queues[i].lock, NULL);
        pthread_cond_init(&queues[i].not_empty, NULL);
    }
}

ipc_error_t queue_enqueue(int dest_id, const ipc_message_t *msg) {
    if (dest_id < 1 || dest_id > MAX_PROCESSES)
        return IPC_ERR_INVALID_DST;

    msg_queue_t *q = &queues[dest_id - 1];
    pthread_mutex_lock(&q->lock);

    if (q->count >= MAX_QUEUE_SIZE) {
        pthread_mutex_unlock(&q->lock);
        return IPC_ERR_QUEUE_FULL;
    }

    insert_sorted(q, msg);
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return IPC_OK;
}

ipc_error_t queue_dequeue(int dest_id, ipc_message_t *out) {
    if (dest_id < 1 || dest_id > MAX_PROCESSES)
        return IPC_ERR_INVALID_DST;

    msg_queue_t *q = &queues[dest_id - 1];
    pthread_mutex_lock(&q->lock);

    while (q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->lock);

    *out = q->msgs[0];
    memmove(&q->msgs[0], &q->msgs[1],
            (size_t)(q->count - 1) * sizeof(ipc_message_t));
    q->count--;

    pthread_mutex_unlock(&q->lock);
    return IPC_OK;
}

ipc_error_t queue_try_dequeue(int dest_id, ipc_message_t *out) {
    if (dest_id < 1 || dest_id > MAX_PROCESSES)
        return IPC_ERR_INVALID_DST;

    msg_queue_t *q = &queues[dest_id - 1];
    pthread_mutex_lock(&q->lock);

    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return IPC_ERR_NO_MSG;
    }

    *out = q->msgs[0];
    memmove(&q->msgs[0], &q->msgs[1],
            (size_t)(q->count - 1) * sizeof(ipc_message_t));
    q->count--;

    pthread_mutex_unlock(&q->lock);
    return IPC_OK;
}

ipc_error_t queue_dequeue_timeout(int dest_id, ipc_message_t *out,
                                   int timeout_ms) {
    if (dest_id < 1 || dest_id > MAX_PROCESSES)
        return IPC_ERR_INVALID_DST;

    struct timespec ts;
    make_timespec(&ts, timeout_ms);

    msg_queue_t *q = &queues[dest_id - 1];
    pthread_mutex_lock(&q->lock);

    while (q->count == 0) {
        int rc = pthread_cond_timedwait(&q->not_empty, &q->lock, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&q->lock);
            return IPC_ERR_TIMEOUT;
        }
    }

    *out = q->msgs[0];
    memmove(&q->msgs[0], &q->msgs[1],
            (size_t)(q->count - 1) * sizeof(ipc_message_t));
    q->count--;

    pthread_mutex_unlock(&q->lock);
    return IPC_OK;
}

int queue_length(int dest_id) {
    if (dest_id < 1 || dest_id > MAX_PROCESSES) return 0;
    msg_queue_t *q = &queues[dest_id - 1];
    pthread_mutex_lock(&q->lock);
    int len = q->count;
    pthread_mutex_unlock(&q->lock);
    return len;
}

void queue_clear(int dest_id) {
    if (dest_id < 1 || dest_id > MAX_PROCESSES) return;
    msg_queue_t *q = &queues[dest_id - 1];
    pthread_mutex_lock(&q->lock);
    q->count = 0;
    pthread_mutex_unlock(&q->lock);
}