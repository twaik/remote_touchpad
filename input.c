#include <stdio.h>
#include <unistd.h>
#include <stdlib.h> // for atoi
#include <string.h> // for strtok
#include <linux/input.h> // for input scan codes
#include <limits.h>
#include "helper.h"

extern int uinputfd;
void init_uinput(int maxX, int maxY);

unsigned int slot = 0;
int status[22][2] = {0};
unsigned int touchcount[2] = {0};
int ids[10] = {0};

int current_mt_slot = 0;
unsigned long long pending_id = 0;

static inline void write_event(int type, int code, int value) {
	dump_event(type, code, value);
	if (uinputfd == -1) 
		init_uinput(status[0][slot], status[1][slot]);
	struct input_event ev = {0};
	gettimeofday(&ev.time, NULL);
	ev.type = type;
	ev.code = code;
	ev.value = value;
	write(uinputfd, &ev, sizeof(ev));
}

static inline int touch_count() {
	int i, c = 0;
	for (i=0; i<10; i++) {
		if (-1 != ids[i]) c++;
	}
	return c;
}

static inline int tool_by_touchcount(int tc) {
	if (tc == 1) return BTN_TOOL_FINGER;
	if (tc == 2) return BTN_TOOL_DOUBLETAP;
	if (tc == 3) return BTN_TOOL_TRIPLETAP;
	if (tc == 4) return BTN_TOOL_QUADTAP;
	if (tc >= 5) return BTN_TOOL_QUINTTAP;
	return -1;
}

static inline void change_tool() {
	int *tc = touchcount;
	if (!tc[slot] != !tc[!slot])
		write_event(EV_KEY, BTN_TOUCH, !!tc[slot]);
	if (tc[slot] != tc[!slot] && tc[slot] <= 5) { 
		for (int i=1; i <= 5; i++) {
			if (i == tc[slot] || i == tc[!slot])
				write_event(EV_KEY, tool_by_touchcount(i), tc[slot]==i);
		}
	}
}

static inline int change_slot(int s) {
	if (current_mt_slot != s) {
		current_mt_slot = s;
		write_event(EV_ABS, ABS_MT_SLOT, s);
	}
	return s;
}

void process_input(char *payload) {
	int i = 0;
	char *token = strtok(payload, ",");
	while(token != NULL && i < 22) {
		status[i++][slot] = atoi(token);
		token = strtok(NULL, ",");
	}
	
	touchcount[slot] = touch_count();

	int xdelta, ydelta, *x, *y;
	for (i = 0; i < 10; i++) {
		x = status[(i+1) * 2    ];
		y = status[(i+1) * 2 + 1];
		xdelta = x[slot] - x[!slot];
		ydelta = y[slot] - y[!slot];
		if (xdelta || ydelta) {
			 if (x[!slot] == -1 || y[!slot] == -1) {
				change_slot(i);
				write_event(EV_ABS, ABS_MT_TRACKING_ID, (ids[i] = pending_id++));
				write_event(EV_ABS, ABS_PRESSURE, 255);
				touchcount[slot] = touch_count();
			} else if (x[slot] == -1 || y[slot] == -1) {
				change_slot(i);
				write_event(EV_ABS, ABS_MT_TRACKING_ID, (ids[i] = -1));
				write_event(EV_ABS, ABS_PRESSURE, 0);
				touchcount[slot] = touch_count();
				continue;
			}
		}
				
		if (xdelta) {
			change_slot(i);
			write_event(EV_ABS, ABS_MT_POSITION_X, x[slot]);
		}
		if (ydelta) {
			change_slot(i);
			write_event(EV_ABS, ABS_MT_POSITION_Y, y[slot]);
		}
	}

	if (pending_id == 65535)
		pending_id = 0;
	change_tool();
	slot = !slot;
	write_event(EV_SYN, 0, 0);
} 
