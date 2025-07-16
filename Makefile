all:
	gcc -o server/server server/main.c
	gcc -o client/client client/main.c

clean:
	rm -f server/server client/client server/server_recv.txt client/*.txt
