CC=gcc
CFLAGS=
CLIENTESRC=cliente.c
SERVERSRC=servidor.c
LIBS=tp_socket.h tp_socket.c
OUT1=cliente
OUT2=servidor

all: tp

tp:
	$(CC) -o $(OUT1) $(CLIENTESRC) $(LIBS) $(CFLAGS)
	$(CC) -o $(OUT2) $(SERVERSRC) $(LIBS) $(CFLAGS)

clean:
	rm -f $(OUT1) $(OUT2)
