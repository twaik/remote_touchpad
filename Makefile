all: build/server

CFLAGS:=-Ibuild

clean:
	@rm -rf build

build/server: build/main.o build/httpd.o build/input.o
	@mkdir -p build
	@gcc $(LDFLAGS) -o build/server $^

build/main.o: main.c pico/httpd.h
	@mkdir -p build
	@gcc $(CFLAGS) -c -o build/main.o main.c

build/input.o: input.c helper.h build/codes.h
	@mkdir -p build
	@gcc $(CFLAGS) -c -o build/input.o input.c

build/httpd.o: pico/httpd.c pico/httpd.h
	@mkdir -p build
	@gcc $(CFLAGS) -c -o build/httpd.o pico/httpd.c

build/codes.h: /usr/include/linux/input-event-codes.h input-event-codes-parser.bash
	@bash input-event-codes-parser.bash $< > $@

.PHONY: clean
