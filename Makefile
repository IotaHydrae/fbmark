PREFIX ?= /usr/local

CFLAGS += -g -O2
BIN_SUFFIX = .out

PROGRAM = fbmark$(BIN_SUFFIX)

all: $(PROGRAM)

$(PROGRAM): fbmark.c
	$(CC) -Wall $^ -o $@ -lm $(CFLAGS) $(LDFLAGS)

clean:
	$(RM) $(PROGRAM)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install $(PROGRAM) $(DESTDIR)$(PREFIX)/bin
