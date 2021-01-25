#include "pico/httpd.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/uinput.h>

static int keep_running = 1;

static void handler(const int signo) {
    keep_running = 0;
    printf("handler\n");
}

static inline void init_signals() {
	struct sigaction sa = {
		.sa_handler = handler,
		.sa_flags = SA_RESTART | SA_NOCLDSTOP,
	};
	if (sigemptyset(&sa.sa_mask) < 0) {
		perror("sigemptyset");
		exit(1);
	}

	if (sigaction(SIGINT, &sa, NULL) < 0) {
		perror("sigaction");
		exit(1);
	}
}

#define die(str, args...) do { \
		dprintf(2, "On line %d:\n\t", __LINE__); \
        perror(str); \
        exit(EXIT_FAILURE); \
    } while(0)

int uinputfd = -1;
void release_uinput() {
    if(ioctl(uinputfd, UI_DEV_DESTROY) < 0)
        die("error: ioctl");
    close(uinputfd);
    uinputfd = -1;
}
void init_uinput(int maxX, int maxY) {
	if (uinputfd != -1) 
		release_uinput();

    struct uinput_user_dev uidev = {0};
    uinputfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(uinputfd < 0)
        die("error: open");

    struct uinput_abs_setup abs;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "sec-touchscreen");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1;
    uidev.id.product = 0x1;
    uidev.id.version = 1;


    if(write(uinputfd, &uidev, sizeof(uidev)) < 0)
        die("error: write");

#define SET(type, code) if (ioctl(uinputfd, UI_SET_##type##BIT, code)) \
		die("error: ioctl");
	SET(EV, EV_LED);
	SET(LED, LED_NUML);
	SET(LED, LED_CAPSL);
	SET(LED, LED_SCROLLL);
	SET(EV, EV_SYN);
	SET(EV, EV_KEY);
	SET(KEY, BTN_TOUCH);
	SET(KEY, BTN_TOOL_FINGER);
	SET(KEY, BTN_TOOL_DOUBLETAP);
	SET(KEY, BTN_TOOL_TRIPLETAP);
	SET(KEY, BTN_TOOL_QUADTAP);
	SET(KEY, BTN_TOOL_QUINTTAP);
	SET(EV, EV_ABS);
	SET(ABS, ABS_MT_SLOT);
	SET(ABS, ABS_MT_TRACKING_ID);
	SET(ABS, ABS_MT_POSITION_X);
	SET(ABS, ABS_MT_POSITION_Y);
	SET(ABS, ABS_MT_TOUCH_MINOR);
	SET(ABS, ABS_MT_TOUCH_MAJOR);
#undef SET
#define SET(c, ...) \
	struct uinput_abs_setup c##_setup = \
		(struct uinput_abs_setup) { \
		.code = c, \
		.absinfo = (struct input_absinfo) { \
			__VA_ARGS__ \
		} \
	}; \
	if (ioctl(uinputfd, UI_ABS_SETUP, &c##_setup) < 0) \
		die("error: ioctl");

	SET(ABS_MT_TRACKING_ID, .maximum = 65535);
	SET(ABS_MT_SLOT, .maximum = 9);
	SET(ABS_MT_TOUCH_MINOR, .maximum = 255);
	SET(ABS_MT_TOUCH_MAJOR, .maximum = 255);
	SET(ABS_MT_POSITION_X, .maximum = maxX, .resolution = 10);
	SET(ABS_MT_POSITION_Y, .maximum = maxY, .resolution = 10);

    if(ioctl(uinputfd, UI_DEV_CREATE) < 0)
        die("error: ioctl");
}

extern int status[22][2];
extern unsigned int ids[10];

int main(int c, char **v) {
	for (int i=0; i<22; i++) for (int j=0; j<=1; j++) status[i][j] = -1;
	for (int i=0; i<10; i++) ids[i] = -1;
	init_signals();
	init_uinput(1000, 1000);

	for (int i=0; i<10; i++) {
		ids[i] = -1;
	}

	serve_forever("8000", &keep_running);
	printf("exiting\n");
	return 0;
}

#define D printf("Near... %d\n", __LINE__)

void write_file_to_fd(const char* filename, int fd) {
	ssize_t bytes_read;
	uint32_t word;
	int indexfd = open(filename, O_RDONLY);
	while (1) {
		bytes_read = read(indexfd, &word, sizeof(word));
		if (bytes_read == -1 || bytes_read == 0) break;
		write(fd, &word, bytes_read);
	}
	close(indexfd);
}

void process_input(char *payload);

void route(int fd, char *method, char *uri, char *qs, char *prot, int payload_size, unsigned char *payload) {
  ROUTE_START()
  ROUTE_GET("/") {
	const char *filename = "index.html";
	if (!access(filename, R_OK)) {
      dprintf(fd, "HTTP/1.1 200 OK\r\n\r\n");
      write_file_to_fd(filename, fd);
    } else {
	  dprintf(fd, "HTTP/1.1 404 Not Found\r\n\r\n");
	  dprintf(fd, "<h1>404</h1>");
    }
  }
  ROUTE_GET("/favicon.ico") {
	dprintf(fd, "HTTP/1.1 404 Not Found\r\n\r\n");
	dprintf(fd, "<h1>404</h1>");
  }
  ROUTE_POST("/input") {
      dprintf(fd, "HTTP/1.1 200 OK\r\n\r\n");
	  if (payload_size) {
		  process_input(payload);
		  dprintf(fd, "OK\n");
	  } else {
		  dprintf(fd, "no payload\n");
		  printf("no payload\n");
	  }
  }
  ROUTE_POST("/debug") {
      dprintf(fd, "HTTP/1.1 200 OK\r\n\r\n");
	  if (payload_size) {
		  printf("%s", payload);
		  dprintf(fd, "OK\n");
	  } else {
		  dprintf(fd, "no payload\n");
		  printf("no payload\n");
	  }
	  printf("\n");
  }
  ROUTE_GET("/test") {
    dprintf(fd, "HTTP/1.1 200 OK\r\n\r\n");
    dprintf(fd, "List of request headers:\r\n\r\n");

    header_t *h = request_headers();

    while (h->name) {
      dprintf(fd, "%s: %s\n", h->name, h->value);
      h++;
    }
  }

  ROUTE_POST("/") {
    dprintf(fd, "HTTP/1.1 200 OK\r\n\r\n");
  }

  ROUTE_END()

}
