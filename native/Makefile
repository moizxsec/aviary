BIN     := aviary
SRCDIR  := src
OBJDIR  := build

CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
PKGS    := cairo x11 xext libcurl libcrypto
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  := $(shell pkg-config --libs $(PKGS)) -lm

SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

PREFIX  ?= $(HOME)/.local

.PHONY: all clean install uninstall check shots service

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
	@echo "installed $(DESTDIR)$(PREFIX)/bin/$(BIN)"

# run the daemon automatically for this user's graphical session
service: install
	@mkdir -p $(HOME)/.config/systemd/user
	@printf '[Unit]\nDescription=Aviary\nAfter=graphical-session.target\nPartOf=graphical-session.target\n\n[Service]\nType=simple\nExecStart=$(PREFIX)/bin/$(BIN) --pixel 2\nRestart=on-failure\nRestartSec=3\n\n[Install]\nWantedBy=graphical-session.target\n' > $(HOME)/.config/systemd/user/aviary.service
	systemctl --user daemon-reload
	systemctl --user enable --now aviary.service
	@echo "aviary will now start with your session" 

uninstall:
	-systemctl --user disable --now aviary.service 2>/dev/null
	rm -f $(HOME)/.config/systemd/user/aviary.service
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -rf $(OBJDIR) $(BIN)
