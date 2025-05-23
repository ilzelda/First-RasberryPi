CROSS = aarch64-linux-gnu
CC = gcc
CROSS_CFLAGS = -I/usr/aarch64-linux-gnu/include -pthread 
CROSS_LDFLAGS = -L/usr/aarch64-linux-gnu/lib -ldl -pthread -lwiringPi -lwiringPiDev -lcrypt 

TARGETS = rpi_server client 

SHARED = led buzzer cds segment
SHARED_SO = $(patsubst %, lib%.so, $(SHARED))

.PHONY: all clean

all: ${TARGETS} ${SHARED_SO}

rpi_server : rpi_server.o daemon.o mythread.o webserver.o
	$(CROSS)-$(CC) -o $@ $^ $(CROSS_LDFLAGS) 
	/home/veda/myscript/scp_proj.sh $@
	/home/veda/myscript/scp_proj.sh activate_server.sh
	/home/veda/myscript/scp_proj.sh index.html
	/home/veda/myscript/scp_proj.sh script.js

rpi_server.o : rpi_server.c
	$(CROSS)-$(CC) -c $(CROSS_CFLAGS) $<
daemon.o : daemon.c
	$(CROSS)-$(CC) -c $(CROSS_CFLAGS) $<
mythread.o : mythread.c
	$(CROSS)-$(CC) -c $(CROSS_CFLAGS) $<
webserver.o : webserver.c
	$(CROSS)-$(CC) -c $(CROSS_CFLAGS) $<

client : client.o
	$(CC) -o $@ client.o 
client.o : client.c
	$(CC) -c $<


lib%.so: %.c
	$(CROSS)-$(CC) -shared -fPIC $(CROSS_CFLAGS) $< -o $@ $(CROSS_LDFLAGS)
	/home/veda/myscript/scp_proj.sh $@

clean:
	rm -f $(TARGETS) $(SHARED_SO) *.o