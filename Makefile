CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -I./include -g
LDFLAGS = -pthread
NCURSES = -lncurses

BINDIR  = bin

SERVER_OBJS = server/server.o   \
              server/registry.o \
              server/queue.o    \
              server/logger.o

.PHONY: all clean dirs server demos tests monitor

all: dirs \
     $(BINDIR)/message_server \
     $(BINDIR)/chat_server    \
     $(BINDIR)/chat_client    \
     $(BINDIR)/priority_demo  \
     $(BINDIR)/broadcast_demo \
     $(BINDIR)/capability_demo\
     $(BINDIR)/benchmark      \
     $(BINDIR)/dashboard

dirs:
	mkdir -p $(BINDIR) logs

server/server.o: server/server.c \
                 include/ipc.h \
                 include/ipc_protocol.h \
                 server/registry.h \
                 server/queue.h \
                 server/logger.h
	$(CC) $(CFLAGS) -c server/server.c -o server/server.o

server/registry.o: server/registry.c server/registry.h include/ipc.h
	$(CC) $(CFLAGS) -c server/registry.c -o server/registry.o

server/queue.o: server/queue.c server/queue.h include/ipc.h
	$(CC) $(CFLAGS) -c server/queue.c -o server/queue.o

server/logger.o: server/logger.c server/logger.h include/ipc.h
	$(CC) $(CFLAGS) -c server/logger.c -o server/logger.o

$(BINDIR)/message_server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(SERVER_OBJS) -o $(BINDIR)/message_server $(LDFLAGS)

client/ipc_client.o: client/ipc_client.c \
                     include/ipc.h \
                     include/ipc_protocol.h
	$(CC) $(CFLAGS) -c client/ipc_client.c -o client/ipc_client.o

libipc.a: client/ipc_client.o
	ar rcs libipc.a client/ipc_client.o

$(BINDIR)/chat_server: demos/chat_server.c libipc.a
	$(CC) $(CFLAGS) demos/chat_server.c \
	      -L. -lipc -o $(BINDIR)/chat_server $(LDFLAGS)

$(BINDIR)/chat_client: demos/chat_client.c libipc.a
	$(CC) $(CFLAGS) demos/chat_client.c \
	      -L. -lipc -o $(BINDIR)/chat_client $(LDFLAGS)

$(BINDIR)/priority_demo: demos/priority_demo.c libipc.a
	$(CC) $(CFLAGS) demos/priority_demo.c \
	      -L. -lipc -o $(BINDIR)/priority_demo $(LDFLAGS)

$(BINDIR)/broadcast_demo: demos/broadcast_demo.c libipc.a
	$(CC) $(CFLAGS) demos/broadcast_demo.c \
	      -L. -lipc -o $(BINDIR)/broadcast_demo $(LDFLAGS)

$(BINDIR)/capability_demo: demos/capability_demo.c libipc.a
	$(CC) $(CFLAGS) demos/capability_demo.c \
	      -L. -lipc -o $(BINDIR)/capability_demo $(LDFLAGS)

$(BINDIR)/benchmark: tests/benchmark.c libipc.a
	$(CC) $(CFLAGS) tests/benchmark.c \
	      -L. -lipc -o $(BINDIR)/benchmark $(LDFLAGS)

$(BINDIR)/dashboard: monitor/dashboard.c \
                     include/ipc.h \
                     include/ipc_protocol.h
	$(CC) $(CFLAGS) monitor/dashboard.c \
	      -o $(BINDIR)/dashboard $(LDFLAGS) $(NCURSES)

clean:
	rm -f server/*.o
	rm -f client/*.o
	rm -f libipc.a
	rm -rf $(BINDIR)
	rm -f /tmp/ipc_server.sock /tmp/ipc_monitor.sock
	@echo "Clean complete."