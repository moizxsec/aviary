BIN     := aviary
SRCDIR  := src
OBJDIR  := build

CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
PKGS    := cairo x11 xext
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  := $(shell pkg-config --libs $(PKGS)) -lm

SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

PREFIX  ?= /usr/local

.PHONY: all clean install uninstall check shots

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/aviary.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# renders the whole delivery to PNGs; fails if any beat never happened
check: $(BIN) | shots
	@mkdir -p shots/pigeon shots/owl shots/swallow
	./$(BIN) render shots 2 phoenix
	./$(BIN) render shots/pigeon 2 pigeon
	./$(BIN) render shots/owl 2 owl
	./$(BIN) render shots/swallow 2 swallow

shots:
	@mkdir -p shots

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -rf $(OBJDIR) $(BIN)
