
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "../include/ipc.h"

#define BENCH_MSGS        5000
#define STRESS_SENDERS    4
#define STRESS_MSGS_EACH  500

static long long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

static FILE *g_report = NULL;

static void rprintf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    if (g_report) {
        va_start(ap, fmt);
        vfprintf(g_report, fmt, ap);
        va_end(ap);
    }
}

static void *echo_server(void *arg) {
    (void)arg;
    int id = ipc_init("BenchServer");
    if (id < 0) return NULL;

    while (1) {
        ipc_message_t msg;
        memset(&msg, 0, sizeof(msg));
        if (ipc_recv(&msg) != IPC_OK) break;
        if (strncmp(msg.data, "STOP", 4) == 0) break;

        ipc_message_t reply = msg;
        ipc_send(msg.sender_id, &reply);
    }
    ipc_shutdown();
    return NULL;
}

typedef struct {
    int        dest_id;
    int        num;
    long long *elapsed_out;
} stress_arg_t;

static void *stress_sender(void *arg) {
    stress_arg_t *sa = (stress_arg_t *)arg;
    char name[32];
    snprintf(name, sizeof(name), "StressSender%d", sa->num);

    int id = ipc_init(name);
    if (id < 0) return NULL;

    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.data, MAX_MSG_SIZE,
             "stress-msg from sender %d", sa->num);
    msg.size     = strlen(msg.data);
    msg.priority = MSG_PRIORITY_NORMAL;

    long long t0 = now_us();
    for (int i = 0; i < STRESS_MSGS_EACH; i++)
        ipc_send(sa->dest_id, &msg);
    *sa->elapsed_out = now_us() - t0;

    ipc_shutdown();
    return NULL;
}

static void test_throughput(int dest_id) {
    rprintf("\n┌─────────────────────────────────────────┐\n");
    rprintf("│  Test 1: Throughput (%d messages)      │\n", BENCH_MSGS);
    rprintf("└─────────────────────────────────────────┘\n");

    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    strcpy(msg.data, "THROUGHPUT");
    msg.size     = strlen(msg.data);
    msg.priority = MSG_PRIORITY_NORMAL;

    long long t0 = now_us();
    for (int i = 0; i < BENCH_MSGS; i++)
        ipc_send(dest_id, &msg);
    long long send_elapsed = now_us() - t0;

    for (int i = 0; i < BENCH_MSGS; i++) {
        ipc_message_t r;
        memset(&r, 0, sizeof(r));
        ipc_recv_timeout(&r, 5000);
    }

    double sec  = send_elapsed / 1e6;
    double rate = BENCH_MSGS / sec;

    rprintf("  Messages sent    : %d\n",   BENCH_MSGS);
    rprintf("  Total time       : %.3f s\n", sec);
    rprintf("  Throughput       : %.0f msg/s\n", rate);
    rprintf("  Avg per message  : %.1f µs\n",
            (double)send_elapsed / BENCH_MSGS);
}

static void test_latency(int dest_id) {
    rprintf("\n┌─────────────────────────────────────────┐\n");
    rprintf("│  Test 2: Round-Trip Latency (%d msgs)  │\n", BENCH_MSGS);
    rprintf("└─────────────────────────────────────────┘\n");

    long long  total    = 0;
    long long  min_rtt  = (long long)2e18;
    long long  max_rtt  = 0;
    long long *samples  = malloc((size_t)BENCH_MSGS * sizeof(long long));
    if (!samples) { rprintf("  malloc failed\n"); return; }

    for (int i = 0; i < BENCH_MSGS; i++) {
        ipc_message_t msg, reply;
        memset(&msg,   0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        strcpy(msg.data, "PING");
        msg.size     = 4;
        msg.priority = MSG_PRIORITY_HIGH;

        long long t0 = now_us();
        ipc_send(dest_id, &msg);
        ipc_recv_timeout(&reply, 5000);
        long long rtt = now_us() - t0;

        samples[i] = rtt;
        total     += rtt;
        if (rtt < min_rtt) min_rtt = rtt;
        if (rtt > max_rtt) max_rtt = rtt;
    }

    for (int i = 0; i < BENCH_MSGS - 1; i++) {
        for (int j = i + 1; j < BENCH_MSGS; j++) {
            if (samples[j] < samples[i]) {
                long long tmp = samples[i];
                samples[i]   = samples[j];
                samples[j]   = tmp;
            }
        }
    }
    long long median = samples[BENCH_MSGS / 2];
    free(samples);

    rprintf("  Avg RTT    : %.1f µs\n",
            (double)total / BENCH_MSGS);
    rprintf("  Median RTT : %lld µs\n", median);
    rprintf("  Min RTT    : %lld µs\n", min_rtt);
    rprintf("  Max RTT    : %lld µs\n", max_rtt);
}

static void test_priority(void) {
    rprintf("\n┌─────────────────────────────────────────┐\n");
    rprintf("│  Test 3: Priority Ordering              │\n");
    rprintf("└─────────────────────────────────────────┘\n");

    int my_id = ipc_get_my_id();

    struct {
        const char    *label;
        msg_priority_t prio;
    } msgs[] = {
        {"LOW_1",      MSG_PRIORITY_LOW},
        {"CRITICAL_1", MSG_PRIORITY_CRITICAL},
        {"NORMAL_1",   MSG_PRIORITY_NORMAL},
        {"HIGH_1",     MSG_PRIORITY_HIGH},
        {"CRITICAL_2", MSG_PRIORITY_CRITICAL},
        {"HIGH_2",     MSG_PRIORITY_HIGH},
        {"LOW_2",      MSG_PRIORITY_LOW},
    };
    int total_msgs = (int)(sizeof(msgs) / sizeof(msgs[0]));

    for (int i = 0; i < total_msgs; i++) {
        ipc_message_t m;
        memset(&m, 0, sizeof(m));
        strncpy(m.data, msgs[i].label, MAX_MSG_SIZE - 1);
        m.size        = strlen(m.data);
        m.priority    = msgs[i].prio;
        m.receiver_id = my_id;
        int rc = ipc_send(my_id, &m);
        if (rc != IPC_OK) rprintf("  [debug] ipc_send returned %d (%s)\n", rc, ipc_strerror(rc));
    }

    const char *expected[] = {
        "CRITICAL_1", "CRITICAL_2",
        "HIGH_1",     "HIGH_2",
        "NORMAL_1",
        "LOW_1",      "LOW_2"
    };

    int pass_count = 0;
    rprintf("  %-14s  %-14s  %s\n", "Expected", "Got", "Result");
    rprintf("  %-14s  %-14s  %s\n", "--------", "---", "------");

    for (int i = 0; i < total_msgs; i++) {
        ipc_message_t out;
        memset(&out, 0, sizeof(out));
        int rc = ipc_recv_timeout(&out, 2000);
        if (rc != IPC_OK) {
            rprintf("  [debug] ipc_recv_timeout returned %d (%s)\n", rc, ipc_strerror(rc));
        }
        int ok = (rc == IPC_OK && strcmp(out.data, expected[i]) == 0);
        if (ok) pass_count++;
        rprintf("  %-14s  %-14s  %s\n",
                expected[i], out.data,
                ok ? "PASS" : "FAIL");
    }
    rprintf("  Result: %d / %d correct\n", pass_count, total_msgs);
}

static void test_stress(int dest_id) {
    rprintf("\n┌─────────────────────────────────────────┐\n");
    rprintf("│  Test 4: Stress (%d senders x %d msgs) │\n",
            STRESS_SENDERS, STRESS_MSGS_EACH);
    rprintf("└─────────────────────────────────────────┘\n");

    pthread_t    tids[STRESS_SENDERS];
    stress_arg_t args[STRESS_SENDERS];
    long long    elapsed[STRESS_SENDERS];

    long long t0 = now_us();

    for (int i = 0; i < STRESS_SENDERS; i++) {
        args[i].dest_id     = dest_id;
        args[i].num         = i + 1;
        args[i].elapsed_out = &elapsed[i];
        pthread_create(&tids[i], NULL, stress_sender, &args[i]);
    }
    for (int i = 0; i < STRESS_SENDERS; i++)
        pthread_join(tids[i], NULL);

    long long total_elapsed = now_us() - t0;
    int       total_msgs    = STRESS_SENDERS * STRESS_MSGS_EACH;

    int drained = 0;
    for (int i = 0; i < total_msgs; i++) {
        ipc_message_t r;
        memset(&r, 0, sizeof(r));
        int rc = ipc_recv_timeout(&r, 3000);
        if (rc == IPC_OK) drained++;
        else              break;
    }

    rprintf("  Senders          : %d\n",   STRESS_SENDERS);
    rprintf("  Messages/sender  : %d\n",   STRESS_MSGS_EACH);
    rprintf("  Total messages   : %d\n",   total_msgs);
    rprintf("  Messages recvd   : %d\n",   drained);
    rprintf("  Data loss        : %d\n",   total_msgs - drained);
    rprintf("  Total time       : %.3f s\n", total_elapsed / 1e6);
    rprintf("  Throughput       : %.0f msg/s\n",
            total_msgs / (total_elapsed / 1e6));
}

int main(void) {
    system("mkdir -p logs");
    g_report = fopen("logs/benchmark_results.txt", "w");
    if (!g_report)
        fprintf(stderr, "Warning: cannot open benchmark log file\n");

    rprintf("╔══════════════════════════════════════════╗\n");
    rprintf("║    Microkernel IPC — Benchmark Suite     ║\n");
    rprintf("╚══════════════════════════════════════════╝\n");

    int my_id = ipc_init("BenchClient");
    if (my_id < 0) {
        fprintf(stderr, "Registration failed: %s\n",
                ipc_strerror(my_id));
        return 1;
    }
    rprintf("BenchClient registered (ID=%d)\n", my_id);

    pthread_t echo_tid;
    pthread_create(&echo_tid, NULL, echo_server, NULL);
    sleep(1);

    int server_id = ipc_get_id_by_name("BenchServer");
    if (server_id < 0) {
        fprintf(stderr, "BenchServer not found\n");
        return 1;
    }
    rprintf("BenchServer found  (ID=%d)\n\n", server_id);

    test_throughput(server_id);
    test_latency(server_id);
    test_priority();
    test_stress(server_id);

    ipc_message_t stop;
    memset(&stop, 0, sizeof(stop));
    strcpy(stop.data, "STOP");
    stop.size     = 4;
    stop.priority = MSG_PRIORITY_CRITICAL;
    ipc_send(server_id, &stop);

    rprintf("\n╔══════════════════════════════════════════╗\n");
    rprintf("║  Benchmark complete.                     ║\n");
    rprintf("║  Results → logs/benchmark_results.txt   ║\n");
    rprintf("╚══════════════════════════════════════════╝\n");

    pthread_join(echo_tid, NULL);
    ipc_shutdown();
    if (g_report) fclose(g_report);
    return 0;
}