#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ncurses.h>

#include "../include/ipc.h"
#include "../include/ipc_protocol.h"

static int g_mon_fd = -1;

static int safe_read(int fd, void *buf, size_t n) {
    size_t total = 0; char *p = (char *)buf;
    while (total < n) {
        ssize_t r = read(fd, p + total, n - total);
        if (r <= 0) return -1;
        total += (size_t)r;
    }
    return 0;
}

static int safe_write(int fd, const void *buf, size_t n) {
    size_t total = 0; const char *p = (const char *)buf;
    while (total < n) {
        ssize_t w = write(fd, p + total, n - total);
        if (w <= 0) return -1;
        total += (size_t)w;
    }
    return 0;
}

static int connect_monitor(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, MONITOR_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    return fd;
}

static monitor_stats_t fetch_stats(void) {
    monitor_stats_t s; memset(&s, 0, sizeof(s));
    if (g_mon_fd < 0) return s;
    monitor_query_t q = MON_QUERY_STATS;
    if (safe_write(g_mon_fd, &q, sizeof(q)) < 0) {
        close(g_mon_fd); g_mon_fd = -1; return s;
    }
    safe_read(g_mon_fd, &s, sizeof(s));
    return s;
}

static void caps_str(unsigned int caps, char *buf, int bufsz) {
    buf[0] = '\0';
    if (caps & CAP_SEND)      strncat(buf, "SEND ",      (size_t)(bufsz - 1));
    if (caps & CAP_RECV)      strncat(buf, "RECV ",      (size_t)(bufsz - 1));
    if (caps & CAP_BROADCAST) strncat(buf, "BROADCAST ", (size_t)(bufsz - 1));
    if (buf[0] == '\0')       strncat(buf, "NONE",       (size_t)(bufsz - 1));
}

static void draw_queue_bar(int y, int x, int len,
                            int val, int max_val, int color) {
    int filled = (max_val > 0) ? (val * len / max_val) : 0;
    attron(COLOR_PAIR(color));
    for (int i = 0; i < filled; i++) mvaddch(y, x + i, ACS_BLOCK);
    attroff(COLOR_PAIR(color));
    for (int i = filled; i < len; i++) mvaddch(y, x + i, '.');
}

int main(void) {
    g_mon_fd = connect_monitor();
    if (g_mon_fd < 0) {
        fprintf(stderr,
                "Cannot connect to %s\n"
                "Make sure the server is running first.\n",
                MONITOR_SOCK_PATH);
        return 1;
    }

    initscr(); cbreak(); noecho(); curs_set(0); timeout(0);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_CYAN,    COLOR_BLACK);
        init_pair(2, COLOR_GREEN,   COLOR_BLACK);
        init_pair(3, COLOR_YELLOW,  COLOR_BLACK);
        init_pair(4, COLOR_RED,     COLOR_BLACK);
        init_pair(5, COLOR_WHITE,   COLOR_BLACK);
        init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
    }

    int ch;
    while ((ch = getch()) != 'q' && ch != 'Q') {
        if (g_mon_fd < 0) g_mon_fd = connect_monitor();

        monitor_stats_t s = fetch_stats();
        int cols = getmaxx(stdscr);
        int row  = 0;
        clear();

        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(row, 0,
                 " Microkernel IPC Monitor  |  press 'q' to quit");
        attroff(COLOR_PAIR(1) | A_BOLD);
        row++;
        for (int c = 0; c < cols; c++) mvaddch(row, c, '=');
        row++;

        attron(A_BOLD);
        mvprintw(row++, 1, "Server Statistics:");
        attroff(A_BOLD);

        long long h   = s.uptime_sec / 3600;
        long long m   = (s.uptime_sec % 3600) / 60;
        long long sec = s.uptime_sec % 60;
        mvprintw(row++, 3, "Uptime        : %02lld:%02lld:%02lld",
                 h, m, sec);
        mvprintw(row++, 3, "Active procs  : %d / %d",
                 s.active_count, MAX_PROCESSES);

        attron(COLOR_PAIR(2));
        mvprintw(row++, 3, "Total sent    : %lld", s.total_sent);
        mvprintw(row++, 3, "Total recvd   : %lld", s.total_recv);
        attroff(COLOR_PAIR(2));

        int err_color = (s.total_errors > 0) ? 4 : 2;
        attron(COLOR_PAIR(err_color));
        mvprintw(row++, 3, "Total errors  : %lld", s.total_errors);
        attroff(COLOR_PAIR(err_color));
        row++;

        for (int c = 0; c < cols; c++) mvaddch(row, c, '-');
        row++;

        attron(A_BOLD);
        mvprintw(row++, 1, "Registered Processes:");
        mvprintw(row, 3,
                 "%-4s %-20s %-6s  %-20s  Capabilities",
                 "ID", "Name", "Q.Len", "Queue [bar 20ch]");
        attroff(A_BOLD);
        row++;

        for (int i = 0; i < s.active_count; i++) {
            int qlen   = s.processes[i].queue_len;
            int qcolor = (qlen > MAX_QUEUE_SIZE * 3 / 4) ? 4 :
                         (qlen > MAX_QUEUE_SIZE / 4)     ? 3 : 2;

            mvprintw(row, 3, "%-4d %-20s %-6d  ",
                     s.processes[i].id,
                     s.processes[i].name,
                     qlen);

            draw_queue_bar(row, 35, 20, qlen, MAX_QUEUE_SIZE, qcolor);

            char cbuf[64];
            caps_str(s.processes[i].caps, cbuf, sizeof(cbuf));
            attron(COLOR_PAIR(6));
            mvprintw(row, 57, " [%s]", cbuf);
            attroff(COLOR_PAIR(6));
            row++;

            if (row >= getmaxy(stdscr) - 3) break;
        }

        if (s.active_count == 0) {
            attron(COLOR_PAIR(3));
            mvprintw(row++, 3, "(no processes registered)");
            attroff(COLOR_PAIR(3));
        }

        row++;
        for (int c = 0; c < cols; c++) mvaddch(row, c, '-');
        row++;
        attron(COLOR_PAIR(5));
        mvprintw(row, 1,
                 "Log: logs/ipc.log  |  Refreshes every 1 s");
        attroff(COLOR_PAIR(5));

        refresh();
        sleep(1);
    }

    endwin();
    if (g_mon_fd >= 0) close(g_mon_fd);
    printf("Monitor closed.\n");
    return 0;
}
