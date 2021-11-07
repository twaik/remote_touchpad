all: server

server: server.c uinput.c
	gcc -o server server.c uinput.c -levent
