CC ?= cc
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/catingiran
SYSCONFDIR ?= $(PREFIX)/etc

CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -DSYSCONFDIR='"$(SYSCONFDIR)"' -DDATADIR='"$(DATADIR)"'
LDFLAGS ?=

TARGET = cti
SRCS = src/main.c src/terminal.c src/config.c src/buffer.c src/syntax.c \
       src/tree.c src/ui.c src/commands.c src/plugins.c src/tools.c \
       src/autocomplete.c src/json.c src/lsp.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

src/%.o: src/%.c src/state.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	ln -sf $(TARGET) "$(DESTDIR)$(BINDIR)/catingiran"
	install -d "$(DESTDIR)$(DATADIR)"
	install -m 644 catingiran.conf "$(DESTDIR)$(DATADIR)/catingiran.conf"
	install -d "$(DESTDIR)$(SYSCONFDIR)"
	install -m 644 catingiran.conf "$(DESTDIR)$(SYSCONFDIR)/catingiran.conf"
	@if [ -z "$(DESTDIR)" ]; then \
		REAL_HOME="$$HOME"; \
		if [ -n "$$SUDO_USER" ]; then \
			REAL_HOME=$$(getent passwd "$$SUDO_USER" | cut -d: -f6); \
		fi; \
		if [ -n "$$REAL_HOME" ]; then \
			mkdir -p "$$REAL_HOME/.config/catingiran/plugins"; \
			if [ ! -f "$$REAL_HOME/.config/catingiran/catingiran.conf" ]; then \
				cp catingiran.conf "$$REAL_HOME/.config/catingiran/catingiran.conf"; \
				echo "Created user config at $$REAL_HOME/.config/catingiran/catingiran.conf"; \
			fi; \
			if [ -n "$$SUDO_USER" ]; then \
				chown -R "$$SUDO_USER:$$SUDO_USER" "$$REAL_HOME/.config/catingiran"; \
			fi; \
		fi; \
	fi
	@echo "Installed catingiran (cti) to $(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(BINDIR)/catingiran"
	rm -f "$(DESTDIR)$(SYSCONFDIR)/catingiran.conf"
	rm -rf "$(DESTDIR)$(DATADIR)"
	@echo "Uninstalled catingiran (cti) from $(DESTDIR)$(PREFIX)"
