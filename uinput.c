#include <stdio.h>
#include <unistd.h>
#include <stdlib.h> // for atoi
#include <stdint.h>
#include <string.h> // for strtok
#include <linux/input.h> // for input scan codes
#include <linux/uinput.h> // for input scan codes
#include <fcntl.h>
#include <limits.h>

#define printf(...) { \
	do { \
		printf(__VA_ARGS__); \
		fsync(1); \
	} while (0); \
}

#define die(str, args...) do { \
		dprintf(2, "On line %d:\n\t", __LINE__); \
        perror(str); \
        exit(EXIT_FAILURE); \
    } while(0)

struct touch {
	int mt_id, x, y;
};

struct touch_slot {
	struct touch touch[10];
};

struct uinput_device {
	int fd;
	uint32_t width, height;
	struct touch_slot slots[2];
	uint8_t current_slot;
	uint8_t current_mt_slot;
	uint16_t pending_mt_id;
} uinput_device = { .fd = -1, .width = 0, .height = 0 };

static inline void release_uinput(struct uinput_device *d) {
	if (d->fd == -1)
		return;
    if(ioctl(d->fd, UI_DEV_DESTROY) < 0)
        die("error: ioctl");
    close(d->fd);
    d->fd = -1;
}

static inline void reset_touch(struct touch* t) {
		t->mt_id = t->x = t->y = -1;
}

static inline void reset_uinput_device(struct uinput_device *d, int width, int height) {
    struct uinput_user_dev uidev = {0};
    struct uinput_abs_setup abs = {0};

	release_uinput(d);
	d->width = width;
	d->height = height;
	for (int i = 0; i < 2; i++)
	for (int j = 0; j < 10; j++)
		reset_touch(&d->slots[i].touch[j]);
	d->current_mt_slot = (uint8_t) -1;
	d->pending_mt_id = 0;

    d->fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(d->fd < 0)
        die("error: open");

    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "sec-touchscreen");
    uidev.id.bustype = BUS_BLUETOOTH;
    uidev.id.vendor  = 0xdecb;
    uidev.id.product = 0xacde;
    uidev.id.version = 1;

    if(write(d->fd, &uidev, sizeof(uidev)) < 0)
        die("error: write");

#define SET(type, code) if (ioctl(d->fd, UI_SET_##type##BIT, code)) \
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
#undef SET
#define SET(c, ...) \
	struct uinput_abs_setup c##_setup = \
		(struct uinput_abs_setup) { \
		.code = c, \
		.absinfo = (struct input_absinfo) { \
			__VA_ARGS__ \
		} \
	}; \
	printf("Setting %d, %d, %d\n",  \
		c##_setup.code, \
		c##_setup.absinfo.minimum, \
		c##_setup.absinfo.maximum); \
	if (ioctl(d->fd, UI_ABS_SETUP, &c##_setup) < 0) \
		die("error: ioctl");

	SET(ABS_PRESSURE, .maximum = 255);
	SET(ABS_MT_TRACKING_ID, .maximum = 65535);
	SET(ABS_MT_SLOT, .maximum = 9);
	SET(ABS_MT_TOUCH_MINOR, .maximum = 255);
	SET(ABS_MT_TOUCH_MAJOR, .maximum = 255);
	SET(ABS_MT_POSITION_X, .maximum = width, .resolution = 10);
	SET(ABS_MT_POSITION_Y, .maximum = height, .resolution = 10);

    if(ioctl(d->fd, UI_DEV_CREATE) < 0)
        die("error: ioctl");
}

static inline void write_event(struct uinput_device *d, int type, int code, int value) {
	//dump_event(type, code, value);
	struct input_event ev = {0};
	gettimeofday(&ev.time, NULL);
	ev.type = type;
	ev.code = code;
	ev.value = value;
	write(d->fd, &ev, sizeof(ev));
}

static inline uint8_t touch_count(struct touch *t) {
	uint8_t i, c = 0;

	for (i=0; i<10; i++)
	if (-1 != -1 != t[i].x && -1 != t[i].y)  {
		c++;
	}

	return c;
}

static inline int tool_by_touchcount(uint8_t tc) {
	if (tc == 1) return BTN_TOOL_FINGER;
	if (tc == 2) return BTN_TOOL_DOUBLETAP;
	if (tc == 3) return BTN_TOOL_TRIPLETAP;
	if (tc == 4) return BTN_TOOL_QUADTAP;
	if (tc >= 5) return BTN_TOOL_QUINTTAP;
	return -1;
}

static inline void change_tool(struct uinput_device *d) {
	uint8_t tc[2], slot = d->current_slot;
	tc[0] = touch_count(d->slots[0].touch);
	tc[1] = touch_count(d->slots[1].touch);
	
	{
		struct touch *t = d->slots[0].touch;
		uint8_t i, c = 0;

		printf("touches: ");
		for (i=0; i<10; i++)
			printf("%d", (-1 != -1 != t[i].x && -1 != t[i].y));
		printf("\n");
		printf("touch count: %d\n", tc[slot]);
		
	}

	if (!tc[slot] != !tc[!slot])
		write_event(d, EV_KEY, BTN_TOUCH, !!tc[slot]);
	if (tc[slot] != tc[!slot] && tc[slot] <= 5) { 
		for (int i=1; i <= 5; i++)
			if (i == tc[slot] || i == tc[!slot]) {
				write_event(d, EV_KEY, tool_by_touchcount(i), tc[slot]==i);
				printf("Changing tool to %d\n", tool_by_touchcount(i));
			}
	}
}

static inline int change_mt_slot(struct uinput_device *d, int s) {
	if (d->current_mt_slot != s) {
		d->current_mt_slot = s;
		write_event(d, EV_ABS, ABS_MT_SLOT, s);
	}
	return s;
}

static inline void process_device(struct uinput_device *d) {
	int xdelta, ydelta, x[2], y[2];
	uint8_t slot = d->current_slot = !d->current_slot;

	for (int i = 0; i < 10; i++) {
		struct touch *t = &d->slots[slot].touch[i];
		x[slot] = d->slots[slot].touch[i].x;
		y[slot] = d->slots[slot].touch[i].y;
		x[!slot] = d->slots[!slot].touch[i].x;
		y[!slot] = d->slots[!slot].touch[i].y;
		if (x[0] - x[1] || y[0] - y[1]) {
			 if (x[!slot] == -1 || y[!slot] == -1) {
				change_mt_slot(d, i);
				write_event(d, EV_ABS, ABS_MT_TRACKING_ID, 
					(t->mt_id = d->pending_mt_id++));
				write_event(d, EV_ABS, ABS_PRESSURE, 255);
			} else if (x[slot] == -1 || y[slot] == -1) {
				usleep(100 * 1000);
				change_mt_slot(d, i);
				write_event(d, EV_ABS, ABS_MT_TRACKING_ID, 
					(t->mt_id = -1));
				write_event(d, EV_ABS, ABS_PRESSURE, 0);
				continue;
			}
		}
				
		if (x[0] - x[1]) {
			change_mt_slot(d, i);
			write_event(d, EV_ABS, ABS_MT_POSITION_X, x[slot]);
		}

		if (y[0] - y[1]) {
			change_mt_slot(d, i);
			write_event(d, EV_ABS, ABS_MT_POSITION_Y, y[slot]);
		}
	}

	change_tool(d);
	write_event(d, EV_SYN, 0, 0);
}

void process_input(char* payload) {
	printf("processing %s\n", payload);
	struct uinput_device *d = &uinput_device;
	uint8_t i = 0;
	int width, height;
	
	char *token = strtok(payload, ",");
	for(i = 0; i < 22; i++) {
		int value = atoi(token);
		token = strtok(NULL, ",");
		if (i == 0) {
			width = value;
		}

		if (i == 1) {
			height = value;
			if ((width != d->width || height != d->height)) {
				printf("resetting uinput with width=%d and height=%d\n", 
					width, height);
				reset_uinput_device(d, width, height);
			};
		}

		if (i >= 2) {
			struct touch *t;
			uint8_t k = i - 2, current = (k - k%2)/2;
			t = &d->slots[!d->current_slot].touch[current];
			if (k % 2 == 0)
				t->x = value;
			if (k % 2 == 1)
				t->y = value;
		}
	}
	
	process_device(d);
}
