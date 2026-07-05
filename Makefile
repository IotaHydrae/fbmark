PREFIX ?= /usr/local

CFLAGS += -g -O2
BIN_SUFFIX = .out

BASENAMES = fb_mandelbrot fb_rectangle fb_sierpinski \
            fb_gradient fb_blit fb_line fb_circle fb_fill \
            fb_plasma fb_scroll fb_text fb_triangle fb_julia \
            fbmark

PROGRAMS = $(addsuffix $(BIN_SUFFIX), $(BASENAMES))

all: $(PROGRAMS)

# Programs that require -lm
fb_sierpinski$(BIN_SUFFIX): fb_sierpinski.c
	$(CC) -Wall $^ -o $@ -lm $(CFLAGS) $(LDFLAGS)
fb_plasma$(BIN_SUFFIX): fb_plasma.c
	$(CC) -Wall $^ -o $@ -lm $(CFLAGS) $(LDFLAGS)
fb_julia$(BIN_SUFFIX): fb_julia.c
	$(CC) -Wall $^ -o $@ -lm $(CFLAGS) $(LDFLAGS)
fbmark$(BIN_SUFFIX): fbmark.c
	$(CC) -Wall $^ -o $@ -lm $(CFLAGS) $(LDFLAGS)

# Pattern rule for programs without special dependencies
%$(BIN_SUFFIX): %.c
	$(CC) -Wall $^ -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	$(RM) $(PROGRAMS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install $(PROGRAMS) $(DESTDIR)$(PREFIX)/bin
