#include "state.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;
static int raw_enabled = 0;
static volatile sig_atomic_t winch_flag = 0;
static char pushback_buf[8];
static int pushback_len = 0;

static void handle_sigwinch(int sig) {
    (void)sig;
    winch_flag = 1;
}

void term_disable_raw(void) {
    if (!raw_enabled) return;
    raw_enabled = 0;
    const char *cleanup =
        "\x1b[?1006l\x1b[?1000l\x1b[?25h\x1b[0m\x1b[?1049l";
    ssize_t unused = write(STDOUT_FILENO, cleanup, strlen(cleanup));
    (void)unused;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void term_enable_raw(void) {
    if (raw_enabled) return;
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(1);
    }
    atexit(term_disable_raw);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(unsigned long)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned long)(OPOST);
    raw.c_cflag |= (unsigned long)(CS8);
    raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; /* 100ms read timeout */

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(1);
    }
    raw_enabled = 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigwinch;
    sigaction(SIGWINCH, &sa, NULL);

    /* Enter alternate screen */
    const char *init_seq = E.cfg.mouse_enabled
        ? "\x1b[?1049h\x1b[?1000h\x1b[?1006h"
        : "\x1b[?1049h";
    ssize_t unused = write(STDOUT_FILENO, init_seq, strlen(init_seq));
    (void)unused;
}

void term_update_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        E.screen_cols = 80;
        E.screen_rows = 24;
    } else {
        E.screen_cols = ws.ws_col;
        E.screen_rows = ws.ws_row;
    }
}

static int read_byte(char *c) {
    if (pushback_len > 0) {
        *c = pushback_buf[--pushback_len];
        return 1;
    }
    int n;
    while ((n = (int)read(STDIN_FILENO, c, 1)) != 1) {
        if (n == -1 && errno != EAGAIN && errno != EINTR) return -1;
        if (n == 0) return 0;
    }
    return 1;
}

static void unread_byte(char c) {
    if (pushback_len < (int)sizeof(pushback_buf)) {
        pushback_buf[pushback_len++] = c;
    }
}

int term_read_key(int *utf8_bytes_out, char utf8_buf[5]) {
    *utf8_bytes_out = 0;
    utf8_buf[0] = '\0';

    char c;
    int r = read_byte(&c);
    if (r <= 0) {
        if (winch_flag) {
            winch_flag = 0;
            term_update_size();
            return KEY_NULL;
        }
        return KEY_NULL;
    }

    if (c == '\x1b') {
        char seq[32];
        if (read_byte(&seq[0]) != 1) return KEY_ESC;
        if (seq[0] != '[' && seq[0] != 'O') {
            unread_byte(seq[0]);
            return KEY_ESC;
        }
        if (read_byte(&seq[1]) != 1) {
            unread_byte(seq[0]);
            return KEY_ESC;
        }

        if (seq[0] == '[') {
            /* SGR Mouse: \x1b[<btn;x;yM or m */
            if (seq[1] == '<') {
                int idx = 0;
                char mbuf[32];
                while (idx < 30) {
                    char mc;
                    if (read_byte(&mc) != 1) break;
                    mbuf[idx++] = mc;
                    if (mc == 'M' || mc == 'm') break;
                }
                mbuf[idx] = '\0';
                int btn = 0, mx = 0, my = 0;
                char term_ch = 'M';
                if (sscanf(mbuf, "%d;%d;%d%c", &btn, &mx, &my, &term_ch) == 4) {
                    E.last_mouse.btn = btn;
                    E.last_mouse.x = mx;
                    E.last_mouse.y = my;
                    E.last_mouse.pressed = (term_ch == 'M');
                    return KEY_MOUSE;
                }
                return KEY_ESC;
            }

            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read_byte(&seq[2]) != 1) return KEY_ESC;
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return KEY_HOME;
                        case '3': return KEY_DEL;
                        case '4': return KEY_END;
                        case '5': return KEY_PAGE_UP;
                        case '6': return KEY_PAGE_DOWN;
                        case '7': return KEY_HOME;
                        case '8': return KEY_END;
                    }
                } else if (seq[2] >= '0' && seq[2] <= '9') {
                    if (read_byte(&seq[3]) == 1 && seq[3] == '~') {
                        if (seq[1] == '1' && seq[2] == '2') return KEY_F2;
                        if (seq[1] == '1' && seq[2] == '5') return KEY_F5;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_ARROW_UP;
                    case 'B': return KEY_ARROW_DOWN;
                    case 'C': return KEY_ARROW_RIGHT;
                    case 'D': return KEY_ARROW_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                    case 'Z': return KEY_SHIFT_TAB;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                case 'Q': return KEY_F2;
            }
        }
        return KEY_ESC;
    }

    unsigned char uc = (unsigned char)c;
    if (uc >= 0xC0) {
        int expected = utf8_char_len(uc);
        if (uc == 0) {
        return KEY_CTRL_SPACE;
    }
    if (uc == 3) {
        return KEY_ESC;
    }

    utf8_buf[0] = c;
        int got = 1;
        while (got < expected) {
            char next_c;
            if (read_byte(&next_c) != 1) break;
            utf8_buf[got++] = next_c;
        }
        utf8_buf[got] = '\0';
        *utf8_bytes_out = got;
        return (int)uc;
    }

    if (uc == 0) {
        return KEY_CTRL_SPACE;
    }
    if (uc == 3) {
        return KEY_ESC;
    }

    utf8_buf[0] = c;
    utf8_buf[1] = '\0';
    *utf8_bytes_out = 1;
    return (int)uc;
}

void ab_append(Abuf *ab, const char *s, int len) {
    if (len <= 0) return;
    if (ab->len + len > ab->cap) {
        int new_cap = ab->cap ? ab->cap * 2 : 4096;
        while (new_cap < ab->len + len) new_cap *= 2;
        char *nb = realloc(ab->b, (size_t)new_cap);
        if (!nb) return;
        ab->b = nb;
        ab->cap = new_cap;
    }
    memcpy(ab->b + ab->len, s, (size_t)len);
    ab->len += len;
}

void ab_append_str(Abuf *ab, const char *s) {
    ab_append(ab, s, (int)strlen(s));
}

void ab_append_fmt(Abuf *ab, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
        ab_append(ab, tmp, n);
    }
}

void ab_append_fg(Abuf *ab, Color c) {
    ab_append_fmt(ab, "\x1b[38;2;%u;%u;%um", c.r, c.g, c.b);
}

void ab_append_bg(Abuf *ab, Color c) {
    ab_append_fmt(ab, "\x1b[48;2;%u;%u;%um", c.r, c.g, c.b);
}

void ab_free(Abuf *ab) {
    free(ab->b);
    ab->b = NULL;
    ab->len = 0;
    ab->cap = 0;
}

int term_wait_input(int timeout_ms) {
    if (pushback_len > 0) return 1;
    if (winch_flag) {
        winch_flag = 0;
        term_update_size();
        return 2;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    int maxfd = STDIN_FILENO;

    for (int i = 0; i < MAX_LSP_SERVERS; i++) {
        if (E.lsp[i].active && E.lsp[i].fd_out >= 0) {
            FD_SET(E.lsp[i].fd_out, &rfds);
            if (E.lsp[i].fd_out > maxfd) maxfd = E.lsp[i].fd_out;
        }
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int r = select(maxfd + 1, &rfds, NULL, NULL, timeout_ms >= 0 ? &tv : NULL);
    if (r < 0) {
        if (winch_flag) {
            winch_flag = 0;
            term_update_size();
            return 2;
        }
        return 0;
    }
    if (r == 0) return 0;
    if (FD_ISSET(STDIN_FILENO, &rfds)) return 1;
    return 2;
}
