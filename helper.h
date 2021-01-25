#include "codes.h"

static inline void dump_event(int type, int code, int value) {
	char buf1[16] = {0}, buf2[16] = {0}, buf3[16] = {0};
	char *t = NULL, *c = NULL, *v = NULL;

	if (type > EV_MAX) {
		printf("BUG: event type %d is bigger than EV_MAX (%d)\n", type, EV_MAX);
	}

	switch(type) {
		#define x(a) case a: t = #a; break;
		EV_CODES
		#undef x
		default: 
		printf("No type found\n");
			snprintf(t = buf1, 16, "0x%X", type);
	}
	
	if (EV_SYN == type) {
		printf("----------------SYN_REPORT----------------\n");
		return;
	}

	if (EV_ABS == type) {
		switch(code) {
			#define x(a) case a: c = #a; break;
			ABS_CODES
			#undef x
			default:
			snprintf(c = buf2, 16, "0x%X", code);
		}
	}
	
	if (EV_KEY == type) {
		switch(code) {
		#define x(a) case a: c = #a; break;
		KEY_CODES
		BTN_CODES
		#undef x
		default:
			snprintf(v = buf3, 16, "0x%X", value);
		}
	}
	
	if (!t) snprintf(t = buf1, 16, "0x%X", type);
	if (!c) snprintf(c = buf2, 16, "0x%X", code);
	if (!v) snprintf(v = buf3, 16, "0x%X", value);
	
	printf("%s: %s %s\n", t, c, v);
	if (EV_SYN == type) printf("\n\n");
} 
