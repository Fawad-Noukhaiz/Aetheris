#include "registry.h"
#include <string.h>
#include <pthread.h>
#include <stdio.h>

static process_entry_t  table[MAX_PROCESSES];
static pthread_mutex_t  reg_mutex = PTHREAD_MUTEX_INITIALIZER;

void registry_init(void) {
    pthread_mutex_lock(&reg_mutex);
    memset(table, 0, sizeof(table));
    pthread_mutex_unlock(&reg_mutex);
}

int registry_register(const char *name, int sock_fd, unsigned int caps) {
    pthread_mutex_lock(&reg_mutex);
    /* prevent duplicate process names */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].active &&
            strncmp(table[i].name, name, MAX_NAME_LEN) == 0) {
            pthread_mutex_unlock(&reg_mutex);
            return -1; /* name already registered */
        }
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!table[i].active) {
            table[i].id        = i + 1;
            table[i].socket_fd = sock_fd;
            table[i].active    = 1;
            table[i].caps      = caps;
            strncpy(table[i].name, name, MAX_NAME_LEN - 1);
            table[i].name[MAX_NAME_LEN - 1] = '\0';
            int id = table[i].id;
            pthread_mutex_unlock(&reg_mutex);
            return id;
        }
    }
    pthread_mutex_unlock(&reg_mutex);
    return -1;
}

void registry_unregister(int id) {
    if (id < 1 || id > MAX_PROCESSES) return;
    pthread_mutex_lock(&reg_mutex);
    memset(&table[id - 1], 0, sizeof(process_entry_t));
    pthread_mutex_unlock(&reg_mutex);
}

int registry_lookup_by_name(const char *name) {
    pthread_mutex_lock(&reg_mutex);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].active &&
            strncmp(table[i].name, name, MAX_NAME_LEN) == 0) {
            int id = table[i].id;
            pthread_mutex_unlock(&reg_mutex);
            return id;
        }
    }
    pthread_mutex_unlock(&reg_mutex);
    return -1;
}

int registry_get_fd(int id) {
    if (id < 1 || id > MAX_PROCESSES) return -1;
    pthread_mutex_lock(&reg_mutex);
    int fd = table[id - 1].active ? table[id - 1].socket_fd : -1;
    pthread_mutex_unlock(&reg_mutex);
    return fd;
}

unsigned int registry_get_caps(int id) {
    if (id < 1 || id > MAX_PROCESSES) return 0;
    pthread_mutex_lock(&reg_mutex);
    unsigned int c = table[id - 1].active ? table[id - 1].caps : 0;
    pthread_mutex_unlock(&reg_mutex);
    return c;
}

int registry_get_all_active(int *ids_out, int max) {
    pthread_mutex_lock(&reg_mutex);
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES && count < max; i++) {
        if (table[i].active)
            ids_out[count++] = table[i].id;
    }
    pthread_mutex_unlock(&reg_mutex);
    return count;
}

void registry_get_snapshot(process_entry_t *out, int *count_out) {
    pthread_mutex_lock(&reg_mutex);
    int c = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].active)
            out[c++] = table[i];
    }
    *count_out = c;
    pthread_mutex_unlock(&reg_mutex);
}