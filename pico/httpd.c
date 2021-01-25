#define __LIBHTTPD__
#include "httpd.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CONNECTIONS 1000

static int listenfd;
static void start_server(const char *);
static void respond(int);

static int clientfd;

static char *buf;

void serve_forever(const char *PORT, int *keepRunning) {
  struct sockaddr_in clientaddr;
  socklen_t addrlen;
  char c;

  int slot = 0;

  printf("Server started %shttp://127.0.0.1:%s%s\n", "\033[92m", PORT,
         "\033[0m");

  // Setting all elements to -1: signifies there is no client connected
  int clientfd;
  start_server(PORT);

  // Ignore SIGCHLD to avoid zombie threads
  signal(SIGCHLD, SIG_IGN);

  // ACCEPT connections
  while (*keepRunning) {
    addrlen = sizeof(clientaddr);
    clientfd = accept(listenfd, (struct sockaddr *)&clientaddr, &addrlen);

    if (clientfd < 0) {
      if (errno == EAGAIN || errno == EINTR) {
        usleep(100);
        continue;
      }
      perror("accept() error");
      exit(1);
    } else {
      respond(clientfd);
      close(clientfd);
    }
  }
}

// start server
void start_server(const char *port) {
  struct addrinfo hints, *res, *p;

  // getaddrinfo for host
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  if (getaddrinfo(NULL, port, &hints, &res) != 0) {
    perror("getaddrinfo() error");
    exit(1);
  }
  // socket and bind
  for (p = res; p != NULL; p = p->ai_next) {
    int option = 1;
    listenfd = socket(p->ai_family, p->ai_socktype, 0);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if (listenfd == -1)
      continue;
    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
      break;
  }
  if (p == NULL) {
    perror("socket() or bind()");
    exit(1);
  }

  freeaddrinfo(res);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 1000;

  if (setsockopt(listenfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                 sizeof(timeout)) < 0) {
    perror("setsockopt failed\n");
    exit(1);
  }

  if (setsockopt(listenfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                 sizeof(timeout)) < 0) {
    perror("setsockopt failed\n");
    exit(1);
  }

  // listen for incoming connections
  if (listen(listenfd, 1000000) != 0) {
    perror("listen() error");
    exit(1);
  }
}

// get request header by name
char *request_header(const char *name) {
  header_t *h = reqhdr;
  while (h->name) {
    if (strcmp(h->name, name) == 0)
      return h->value;
    h++;
  }
  return NULL;
}

// get all request headers
header_t *request_headers(void) { return reqhdr; }

// client connection
void respond(int clientfd) {
  char *method = NULL;
  char *uri = NULL;
  char *qs = NULL;
  char *prot = NULL;
  char *payload = NULL;
  int payload_size = 0;

  int rcvd, fd, bytes_read;
  char *ptr;
  // int buf_size = 65535;
  int buf_size = 1024;

  buf = malloc(buf_size);
  memset(buf, 0, buf_size);
  rcvd = recv(clientfd, buf, buf_size, 0);

  if (rcvd < 0) { // receive error
    if (errno == EAGAIN)
      return;
    perror("recv() error\n");
  } else if (rcvd == 0) // receive socket closed
    fprintf(stderr, "Client disconnected unexpectedly.\n");
  else // message received
  {
    buf[rcvd] = '\0';

    method = strtok(buf, " \t\r\n");
    uri = strtok(NULL, " \t");
    prot = strtok(NULL, " \t\r\n");

	#ifdef DEBUG_HEADERS
    fprintf(stderr, "\x1b[32m + [%s] %s\x1b[0m\n", method, uri);
    #endif

    qs = strchr(uri, '?');

    if (qs) {
      *qs++ = '\0'; // split URI
    } else {
      qs = uri - 1; // use an empty string
    }

    header_t *h = reqhdr;
    char *t, *t2;
    while (h < reqhdr + 16) {
      char *k, *v;

      k = strtok(NULL, "\r\n: \t");
      if (!k)
        break;

      v = strtok(NULL, "\r\n");
      while (*v && *v == ' ')
        v++;

      h->name = k;
      h->value = v;
      h++;
#ifdef DEBUG_HEADERS
      fprintf(stderr, "[H] %s: %s\n", k, v);
#endif
      t = v + 1 + strlen(v);
      if (t[1] == '\r' && t[2] == '\n')
        break;
    }
    t += 3; // now the *t shall be the beginning of user payload
    t2 = request_header("Content-Length"); // and the related header if there is
    payload_size = t2 ? atol(t2) : (rcvd - (t - buf));
    payload = payload_size ? t : NULL;

    // call router
    route(clientfd, method, uri, qs, prot, payload_size, payload);

    fsync(clientfd);
    shutdown(clientfd, SHUT_RDWR);
    close(clientfd);
  }
  free(buf);
}
