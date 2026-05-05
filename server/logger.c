#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>

static FILE           *log_fp    = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static long long g_total_sent   = 0;
static long long g_total_recv   = 0;
static long long g_total_errors = 0;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static const char *priority_str(msg_priority_t p) {
    switch (p) {
        case MSG_PRIORITY_LOW:      return "LOW";
        case MSG_PRIORITY_NORMAL:   return "NORMAL";
        case MSG_PRIORITY_HIGH:     return "HIGH";
        case MSG_PRIORITY_CRITICAL: return "CRITICAL";
        default:                    return "UNKNOWN";
    }
}

void logger_init(const char *filepath) {
    pthread_mutex_lock(&log_mutex);
    log_fp = fopen(filepath, "a");
    if (log_fp) {
        /* make sure log file is owner-read/write only */
        if (fchmod(fileno(log_fp), S_IRUSR | S_IWUSR) < 0) {
            /* non-fatal */
            perror("fchmod log file");
        }
        fprintf(log_fp,
                "timestamp_ms,event,sender,receiver,"
                "msg_id,priority,size\n");
        fflush(log_fp);
    }
    pthread_mutex_unlock(&log_mutex);
}

void logger_shutdown(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_fp) { fclose(log_fp); log_fp = NULL; }
    pthread_mutex_unlock(&log_mutex);
}

void logger_log(const char *event, int sender, int receiver,
                int msg_id, msg_priority_t priority, size_t size) {
    pthread_mutex_lock(&log_mutex);

    if      (strncmp(event, "SEND",  4) == 0) g_total_sent++;
    else if (strncmp(event, "BCAST", 5) == 0) g_total_sent++;
    else if (strncmp(event, "RECV",  4) == 0) g_total_recv++;
    else if (strncmp(event, "ERR",   3) == 0) g_total_errors++;

    if (log_fp) {
        fprintf(log_fp, "%lld,%s,%d,%d,%d,%s,%zu\n",
                now_ms(), event, sender, receiver,
                msg_id, priority_str(priority), size);
        fflush(log_fp);
    }
    pthread_mutex_unlock(&log_mutex);
}

long long logger_total_sent(void) {
    pthread_mutex_lock(&log_mutex);
    long long v = g_total_sent;
    pthread_mutex_unlock(&log_mutex);
    return v;
}

long long logger_total_recv(void) {
    pthread_mutex_lock(&log_mutex);
    long long v = g_total_recv;
    pthread_mutex_unlock(&log_mutex);
    return v;
}

long long logger_total_errors(void) {
    pthread_mutex_lock(&log_mutex);
    long long v = g_total_errors;
    pthread_mutex_unlock(&log_mutex);
    return v;
}