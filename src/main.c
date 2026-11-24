#include "state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

EditorState E;

static int map_russian_normal(const char *u) {
    struct Map { const char *ru; int en; };
    static const struct Map table[] = {
        {"ж", ':'}, {"Ж", ':'},
        {"р", 'h'}, {"о", 'j'}, {"л", 'k'}, {"д", 'l'},
        {"ш", 'i'}, {"Ш", 'I'}, {"ф", 'a'}, {"Ф", 'A'},
        {"щ", 'o'}, {"Щ", 'O'}, {"ц", 'w'}, {"и", 'b'},
        {"п", 'g'}, {"П", 'G'}, {"ч", 'x'}, {"в", 'd'},
        {"н", 'y'}, {"з", 'p'}, {"г", 'u'}, {"т", 'n'},
        {"Т", 'N'}, {"м", 'v'}, {"у", 'e'}, {"с", 'c'},
        {"й", 'q'}, {"ы", 's'}, {"к", 'r'}, {"Р", 'H'},
        {"Н", 'Y'}, {NULL, 0}
    };
    for (int i = 0; table[i].ru; i++) {
        if (strcmp(u, table[i].ru) == 0) return table[i].en;
    }
    return 0;
}

static void clamp_cursor(Buffer *b) {
    if (b->cy < 0) b->cy = 0;
    if (b->cy >= b->num_lines) b->cy = b->num_lines - 1;
    Line *ln = &b->lines[b->cy];
    if (b->cx < 0) b->cx = 0;
    if (b->cx > ln->size) b->cx = ln->size;
    /* In Normal/Visual mode, keep cursor on valid char start if line non-empty */
    if (E.mode != MODE_INSERT && b->cx == ln->size && ln->size > 0) {
        b->cx = utf8_prev_idx(ln->chars, ln->size);
    }
}

static void move_cursor(Buffer *b, int key) {
    Line *ln = &b->lines[b->cy];
    switch (key) {
        case KEY_ARROW_LEFT:
        case 'h':
            if (b->cx > 0) b->cx = utf8_prev_idx(ln->chars, b->cx);
            break;
        case KEY_ARROW_RIGHT:
        case 'l':
            if (b->cx < ln->size) b->cx = utf8_next_idx(ln->chars, ln->size, b->cx);
            break;
        case KEY_ARROW_UP:
        case 'k':
            if (b->cy > 0) b->cy--;
            break;
        case KEY_ARROW_DOWN:
        case 'j':
            if (b->cy + 1 < b->num_lines) b->cy++;
            break;
        case KEY_HOME:
        case '0':
            b->cx = 0;
            break;
        case KEY_END:
        case '$':
            b->cx = ln->size;
            break;
        case KEY_PAGE_UP:
        case CTRL_KEY('u'):
            b->cy -= (E.screen_rows / 2);
            if (b->cy < 0) b->cy = 0;
            break;
        case KEY_PAGE_DOWN:
        case CTRL_KEY('d'):
            b->cy += (E.screen_rows / 2);
            if (b->cy >= b->num_lines) b->cy = b->num_lines - 1;
            break;
    }
    clamp_cursor(b);
}

static void move_word_forward(Buffer *b) {
    Line *ln = &b->lines[b->cy];
    int i = b->cx;
    while (i < ln->size && !isspace((unsigned char)ln->chars[i])) i++;
    while (i < ln->size && isspace((unsigned char)ln->chars[i])) i++;
    if (i >= ln->size && b->cy + 1 < b->num_lines) {
        b->cy++;
        b->cx = 0;
    } else {
        b->cx = i;
    }
    clamp_cursor(b);
}

static void move_word_backward(Buffer *b) {
    if (b->cx == 0 && b->cy > 0) {
        b->cy--;
        b->cx = b->lines[b->cy].size;
        clamp_cursor(b);
        return;
    }
    Line *ln = &b->lines[b->cy];
    int i = b->cx;
    while (i > 0 && isspace((unsigned char)ln->chars[i - 1])) i--;
    while (i > 0 && !isspace((unsigned char)ln->chars[i - 1])) i--;
    b->cx = i;
    clamp_cursor(b);
}

static void paste_clipboard(Buffer *b) {
    if (!E.clipboard || !E.clipboard[0]) return;
    size_t len = strlen(E.clipboard);
    if (E.clipboard[len - 1] == '\n') {
        /* Linewise paste below current line */
        b->cx = b->lines[b->cy].size;
        int old_ai = E.cfg.auto_indent;
        E.cfg.auto_indent = 0;
        buf_insert_newline(b);
        E.cfg.auto_indent = old_ai;
        buf_insert_bytes(b, E.clipboard, (int)(len - 1));
        b->cx = 0;
    } else {
        if (b->cx < b->lines[b->cy].size) {
            b->cx = utf8_next_idx(b->lines[b->cy].chars, b->lines[b->cy].size, b->cx);
        }
        for (size_t i = 0; i < len; i++) {
            if (E.clipboard[i] == '\n') {
                int old_ai = E.cfg.auto_indent;
                E.cfg.auto_indent = 0;
                buf_insert_newline(b);
                E.cfg.auto_indent = old_ai;
            } else {
                buf_insert_bytes(b, &E.clipboard[i], 1);
            }
        }
    }
}

static void handle_mouse(void) {
    if (!E.cfg.mouse_enabled) return;
    int mx = E.last_mouse.x; /* 1-based */
    int my = E.last_mouse.y; /* 1-based */
    int btn = E.last_mouse.btn;

    int top_rows = E.cfg.show_tabs ? 1 : 0;
    int sb_w = (E.sidebar_visible && E.screen_cols > E.cfg.sidebar_width + 12)
               ? E.cfg.sidebar_width : 0;

    Buffer *b = &E.buffers[E.cur_buf];

    /* Scroll wheel */
    if (btn == 64 || btn == 65) {
        int dir = (btn == 64) ? -3 : 3;
        if (sb_w > 0 && mx <= sb_w) {
            E.tree_sel += (dir > 0 ? 1 : -1);
            if (E.tree_sel < 0) E.tree_sel = 0;
            if (E.tree_sel >= E.tree_len) E.tree_sel = E.tree_len > 0 ? E.tree_len - 1 : 0;
        } else {
            b->cy += dir;
            clamp_cursor(b);
        }
        return;
    }

    if (btn != 0 || !E.last_mouse.pressed) return;

    /* Click on top tab bar */
    if (E.cfg.show_tabs && my == 1) {
        int col = 1;
        for (int i = 0; i < E.num_buffers; i++) {
            const char *nm = E.buffers[i].filename ? E.buffers[i].filename : "[No Name]";
            const char *sl = strrchr(nm, '/');
            if (sl) nm = sl + 1;
            int w = 4 + (int)strlen(nm) + (E.buffers[i].dirty ? 1 : 0) + 1;
            if (mx >= col && mx < col + w) {
                E.cur_buf = i;
                E.focus = FOCUS_EDITOR;
                return;
            }
            col += w;
        }
        return;
    }

    if (my <= top_rows || my >= E.screen_rows - 1) return;

    /* Click in sidebar */
    if (sb_w > 0 && mx <= sb_w) {
        E.focus = FOCUS_SIDEBAR;
        int rel_row = my - top_rows - 1;
        if (rel_row >= 1) {
            int idx = E.tree_scroll + (rel_row - 1);
            if (idx >= 0 && idx < E.tree_len) {
                E.tree_sel = idx;
                tree_toggle_selected();
            }
        }
        return;
    }

    /* Click in editor */
    if (mx > sb_w + (sb_w > 0 ? 1 : 0)) {
        E.focus = FOCUS_EDITOR;
        int gutter_w = E.cfg.line_numbers ? 6 : 1;
        int target_y = b->rowoff + (my - top_rows - 1);
        if (target_y < 0) target_y = 0;
        if (target_y >= b->num_lines) target_y = b->num_lines - 1;
        b->cy = target_y;

        int target_rx = b->coloff + (mx - (sb_w + (sb_w > 0 ? 1 : 0) + gutter_w + 1));
        if (target_rx < 0) target_rx = 0;
        Line *ln = &b->lines[b->cy];
        int rx = 0, i = 0;
        while (i < ln->size && rx < target_rx) {
            unsigned char c = (unsigned char)ln->chars[i];
            int cl = utf8_char_len(c);
            rx += (c == '\t') ? E.cfg.tab_size : ((cl == 4) ? 2 : 1);
            i += cl;
        }
        b->cx = i;
        clamp_cursor(b);
    }
}

static void handle_sidebar_key(int key, const char *ubuf) {
    if (key >= 128 && key < 1000 && E.cfg.russian_keymap) {
        int mapped = map_russian_normal(ubuf);
        if (mapped) key = mapped;
    }

    switch (key) {
        case CTRL_KEY('b'):
        case KEY_F2:
            E.sidebar_visible = 0;
            E.focus = FOCUS_EDITOR;
            break;
        case CTRL_KEY('l'):
        case KEY_TAB:
        case KEY_ESC:
            E.focus = FOCUS_EDITOR;
            break;
        case 'j':
        case KEY_ARROW_DOWN:
            if (E.tree_sel + 1 < E.tree_len) E.tree_sel++;
            break;
        case 'k':
        case KEY_ARROW_UP:
            if (E.tree_sel > 0) E.tree_sel--;
            break;
        case 'l':
        case KEY_ARROW_RIGHT:
        case KEY_ENTER:
            tree_toggle_selected();
            break;
        case 'h':
        case KEY_ARROW_LEFT:
        case KEY_BACKSPACE:
            tree_collapse_or_parent();
            break;
        case 'a':
            tree_create_prompt();
            break;
        case 'd':
            tree_delete_selected();
            break;
        case 'r':
            tree_rename_selected();
            break;
        case 'R':
            tree_refresh();
            editor_set_status("Explorer refreshed");
            break;
        case 'H':
            E.cfg.show_hidden = !E.cfg.show_hidden;
            tree_refresh();
            editor_set_status("Hidden files: %s", E.cfg.show_hidden ? "shown" : "hidden");
            break;
        case ':':
            E.mode = MODE_COMMAND;
            E.cmd_len = 0;
            E.cmd_pos = 0;
            E.cmd_buf[0] = '\0';
            E.cmd_hist_idx = E.cmd_hist_len;
            break;
        case 'q':
            cmd_execute("q");
            break;
    }
}

static void handle_command_key(int key, int ulen, const char *ubuf) {
    if (key == KEY_ESC) {
        E.mode = MODE_NORMAL;
        return;
    }
    if (key == KEY_ENTER) {
        E.mode = MODE_NORMAL;
        cmd_execute(E.cmd_buf);
        return;
    }
    if (key == KEY_TAB) {
        cmd_autocomplete(E.cmd_buf, &E.cmd_len, &E.cmd_pos, sizeof(E.cmd_buf));
        return;
    }
    if (key == KEY_ARROW_UP) {
        if (E.cmd_hist_len > 0 && E.cmd_hist_idx > 0) {
            E.cmd_hist_idx--;
            snprintf(E.cmd_buf, sizeof(E.cmd_buf), "%s", E.cmd_history[E.cmd_hist_idx]);
            E.cmd_len = (int)strlen(E.cmd_buf);
            E.cmd_pos = E.cmd_len;
        }
        return;
    }
    if (key == KEY_ARROW_DOWN) {
        if (E.cmd_hist_idx + 1 < E.cmd_hist_len) {
            E.cmd_hist_idx++;
            snprintf(E.cmd_buf, sizeof(E.cmd_buf), "%s", E.cmd_history[E.cmd_hist_idx]);
            E.cmd_len = (int)strlen(E.cmd_buf);
            E.cmd_pos = E.cmd_len;
        } else {
            E.cmd_hist_idx = E.cmd_hist_len;
            E.cmd_buf[0] = '\0';
            E.cmd_len = 0;
            E.cmd_pos = 0;
        }
        return;
    }
    if (key == KEY_BACKSPACE || key == CTRL_KEY('h')) {
        if (E.cmd_pos > 0) {
            int prev = utf8_prev_idx(E.cmd_buf, E.cmd_pos);
            int d = E.cmd_pos - prev;
            memmove(E.cmd_buf + prev, E.cmd_buf + E.cmd_pos, (size_t)(E.cmd_len - E.cmd_pos + 1));
            E.cmd_len -= d;
            E.cmd_pos = prev;
        } else if (E.cmd_len == 0) {
            E.mode = MODE_NORMAL;
        }
        return;
    }
    if (ulen > 0 && (unsigned char)ubuf[0] >= 32 && key < 1000) {
        if (E.cmd_len + ulen < (int)sizeof(E.cmd_buf) - 1) {
            memmove(E.cmd_buf + E.cmd_pos + ulen, E.cmd_buf + E.cmd_pos,
                    (size_t)(E.cmd_len - E.cmd_pos + 1));
            memcpy(E.cmd_buf + E.cmd_pos, ubuf, (size_t)ulen);
            E.cmd_len += ulen;
            E.cmd_pos += ulen;
        }
    }
}

static void handle_search_key(int key, int ulen, const char *ubuf) {
    Buffer *b = &E.buffers[E.cur_buf];
    if (key == KEY_ESC) {
        E.mode = MODE_NORMAL;
        E.search_len = 0;
        E.search_buf[0] = '\0';
        return;
    }
    if (key == KEY_ENTER) {
        E.mode = MODE_NORMAL;
        buf_find(b, E.search_buf, 1);
        return;
    }
    if (key == KEY_BACKSPACE || key == CTRL_KEY('h')) {
        if (E.search_len > 0) {
            E.search_len = utf8_prev_idx(E.search_buf, E.search_len);
            E.search_buf[E.search_len] = '\0';
        } else {
            E.mode = MODE_NORMAL;
        }
        return;
    }
    if (ulen > 0 && (unsigned char)ubuf[0] >= 32 && key < 1000) {
        if (E.search_len + ulen < (int)sizeof(E.search_buf) - 1) {
            memcpy(E.search_buf + E.search_len, ubuf, (size_t)ulen);
            E.search_len += ulen;
            E.search_buf[E.search_len] = '\0';
        }
    }
}

static void handle_insert_key(int key, int ulen, const char *ubuf) {
    Buffer *b = &E.buffers[E.cur_buf];
    if (key == KEY_ESC) {
        E.mode = MODE_NORMAL;
        if (b->cx > 0) b->cx = utf8_prev_idx(b->lines[b->cy].chars, b->cx);
        clamp_cursor(b);
        return;
    }

    switch (key) {
        case KEY_ENTER:
            buf_insert_newline(b);
            break;
        case KEY_BACKSPACE:
            buf_backspace(b);
            break;
        case KEY_DEL:
            buf_delete_at_cursor(b);
            break;
        case KEY_TAB:
            if (E.cfg.expand_tab) {
                int ts = E.cfg.tab_size > 0 ? E.cfg.tab_size : 4;
                char sp[17];
                if (ts > 16) ts = 16;
                memset(sp, ' ', (size_t)ts);
                buf_insert_bytes(b, sp, ts);
            } else {
                buf_insert_bytes(b, "\t", 1);
            }
            break;
        case CTRL_KEY('h'):
        case KEY_ARROW_LEFT:
            move_cursor(b, KEY_ARROW_LEFT);
            break;
        case CTRL_KEY('j'):
        case KEY_ARROW_DOWN:
            move_cursor(b, KEY_ARROW_DOWN);
            break;
        case CTRL_KEY('k'):
        case KEY_ARROW_UP:
            move_cursor(b, KEY_ARROW_UP);
            break;
        case CTRL_KEY('l'):
        case KEY_ARROW_RIGHT:
            move_cursor(b, KEY_ARROW_RIGHT);
            break;
        case KEY_HOME:
        case KEY_END:
        case KEY_PAGE_UP:
        case KEY_PAGE_DOWN:
            move_cursor(b, key);
            break;
        default:
            if (ulen > 0 && (unsigned char)ubuf[0] >= 32 && key < 1000) {
                if (E.cfg.auto_pairs && ulen == 1) {
                    const char *pair = NULL;
                    switch (ubuf[0]) {
                        case '(': pair = "()"; break;
                        case '[': pair = "[]"; break;
                        case '{': pair = "{}"; break;
                        case '"': pair = "\"\""; break;
                        case '\'': pair = "''"; break;
                    }
                    if (pair) {
                        buf_insert_bytes(b, pair, 2);
                        b->cx--;
                        break;
                    }
                }
                buf_insert_bytes(b, ubuf, ulen);
            }
            break;
    }
}

static void handle_visual_key(int key, const char *ubuf) {
    Buffer *b = &E.buffers[E.cur_buf];
    if (key >= 128 && key < 1000 && E.cfg.russian_keymap) {
        int mapped = map_russian_normal(ubuf);
        if (mapped) key = mapped;
    }

    int sy = b->vis_sy, ey = b->cy;
    if (sy > ey) { int t = sy; sy = ey; ey = t; }

    switch (key) {
        case KEY_ESC:
        case 'v':
            E.mode = MODE_NORMAL;
            break;
        case 'h': case 'j': case 'k': case 'l':
        case KEY_ARROW_LEFT: case KEY_ARROW_DOWN: case KEY_ARROW_UP: case KEY_ARROW_RIGHT:
        case '0': case '$':
            move_cursor(b, key);
            break;
        case 'y': {
            size_t total = 0;
            for (int r = sy; r <= ey; r++) total += (size_t)b->lines[r].size + 1;
            free(E.clipboard);
            E.clipboard = malloc(total + 1);
            if (E.clipboard) {
                size_t off = 0;
                for (int r = sy; r <= ey; r++) {
                    memcpy(E.clipboard + off, b->lines[r].chars, (size_t)b->lines[r].size);
                    off += (size_t)b->lines[r].size;
                    E.clipboard[off++] = '\n';
                }
                E.clipboard[off] = '\0';
            }
            E.mode = MODE_NORMAL;
            editor_set_status("%d line(s) yanked", ey - sy + 1);
            break;
        }
        case 'd':
        case 'x':
            for (int r = ey; r >= sy; r--) buf_delete_line(b, r);
            E.mode = MODE_NORMAL;
            break;
        case '>':
            for (int r = sy; r <= ey; r++) buf_indent_line(b, r, 1);
            E.mode = MODE_NORMAL;
            break;
        case '<':
            for (int r = sy; r <= ey; r++) buf_indent_line(b, r, -1);
            E.mode = MODE_NORMAL;
            break;
    }
}

static void handle_normal_key(int key, const char *ubuf) {
    Buffer *b = &E.buffers[E.cur_buf];

    if (key >= 128 && key < 1000 && E.cfg.russian_keymap) {
        int mapped = map_russian_normal(ubuf);
        if (mapped) key = mapped;
    }

    /* Leader sequences (<Space> + ...) */
    if (E.pending_leader) {
        E.pending_leader = 0;
        if (key == 'e') {
            E.sidebar_visible = !E.sidebar_visible;
            E.focus = E.sidebar_visible ? FOCUS_SIDEBAR : FOCUS_EDITOR;
            if (E.sidebar_visible) tree_refresh();
        } else if (key == 'w') {
            buf_save(b, NULL);
        } else if (key == 'q') {
            cmd_execute("q");
        } else if (key == 'c') {
            buf_close(E.cur_buf, 0);
        } else if (key >= '1' && key <= '9') {
            int idx = key - '1';
            if (idx < E.num_buffers) E.cur_buf = idx;
        }
        return;
    }

    if (E.pending_g) {
        E.pending_g = 0;
        if (key == 'g') {
            b->cy = 0;
            b->cx = 0;
            clamp_cursor(b);
        }
        return;
    }

    if (E.pending_d) {
        E.pending_d = 0;
        if (key == 'd') buf_delete_line(b, b->cy);
        return;
    }

    if (E.pending_y) {
        E.pending_y = 0;
        if (key == 'y') {
            Line *ln = &b->lines[b->cy];
            free(E.clipboard);
            E.clipboard = malloc((size_t)ln->size + 2);
            if (E.clipboard) {
                memcpy(E.clipboard, ln->chars, (size_t)ln->size);
                E.clipboard[ln->size] = '\n';
                E.clipboard[ln->size + 1] = '\0';
            }
            editor_set_status("1 line yanked");
        }
        return;
    }

    switch (key) {
        case ' ':
            E.pending_leader = 1;
            break;
        case 'g':
            E.pending_g = 1;
            break;
        case 'G':
            b->cy = b->num_lines - 1;
            b->cx = 0;
            clamp_cursor(b);
            break;
        case 'd':
            E.pending_d = 1;
            break;
        case 'y':
            E.pending_y = 1;
            break;
        case 'p':
            paste_clipboard(b);
            break;
        case 'u':
            buf_undo(b);
            break;
        case 'x':
            buf_delete_at_cursor(b);
            clamp_cursor(b);
            break;
        case 'h': case 'j': case 'k': case 'l':
        case KEY_ARROW_LEFT: case KEY_ARROW_DOWN: case KEY_ARROW_UP: case KEY_ARROW_RIGHT:
        case '0': case '$': case KEY_HOME: case KEY_END:
        case KEY_PAGE_UP: case KEY_PAGE_DOWN:
        case CTRL_KEY('u'): case CTRL_KEY('d'):
            move_cursor(b, key);
            break;
        case 'w':
            move_word_forward(b);
            break;
        case 'b':
            move_word_backward(b);
            break;
        case 'i':
            E.mode = MODE_INSERT;
            break;
        case 'I':
            b->cx = 0;
            while (b->cx < b->lines[b->cy].size &&
                   (b->lines[b->cy].chars[b->cx] == ' ' || b->lines[b->cy].chars[b->cx] == '\t')) {
                b->cx++;
            }
            E.mode = MODE_INSERT;
            break;
        case 'a':
            if (b->cx < b->lines[b->cy].size) {
                b->cx = utf8_next_idx(b->lines[b->cy].chars, b->lines[b->cy].size, b->cx);
            }
            E.mode = MODE_INSERT;
            break;
        case 'A':
            b->cx = b->lines[b->cy].size;
            E.mode = MODE_INSERT;
            break;
        case 'o':
            b->cx = b->lines[b->cy].size;
            buf_insert_newline(b);
            E.mode = MODE_INSERT;
            break;
        case 'O': {
            b->cx = 0;
            buf_insert_newline(b);
            b->cy--;
            b->cx = 0;
            E.mode = MODE_INSERT;
            break;
        }
        case 'v':
            b->vis_sx = b->cx;
            b->vis_sy = b->cy;
            E.mode = MODE_VISUAL;
            break;
        case ':':
            E.mode = MODE_COMMAND;
            E.cmd_len = 0;
            E.cmd_pos = 0;
            E.cmd_buf[0] = '\0';
            E.cmd_hist_idx = E.cmd_hist_len;
            break;
        case '/':
            E.mode = MODE_SEARCH;
            E.search_len = 0;
            E.search_buf[0] = '\0';
            break;
        case 'n':
            if (E.search_len > 0) buf_find(b, E.search_buf, 1);
            break;
        case 'N':
            if (E.search_len > 0) buf_find(b, E.search_buf, -1);
            break;
        case KEY_ESC:
            E.search_len = 0;
            E.search_buf[0] = '\0';
            break;
        case CTRL_KEY('b'):
        case KEY_F2:
            E.sidebar_visible = !E.sidebar_visible;
            E.focus = E.sidebar_visible ? FOCUS_SIDEBAR : FOCUS_EDITOR;
            if (E.sidebar_visible) tree_refresh();
            break;
        case CTRL_KEY('h'):
            if (E.sidebar_visible) E.focus = FOCUS_SIDEBAR;
            break;
        case CTRL_KEY('l'):
            E.focus = FOCUS_EDITOR;
            break;
        case KEY_TAB:
            E.cur_buf = (E.cur_buf + 1) % E.num_buffers;
            break;
        case KEY_SHIFT_TAB:
            E.cur_buf = (E.cur_buf - 1 + E.num_buffers) % E.num_buffers;
            break;
        case '>':
            buf_indent_line(b, b->cy, 1);
            break;
        case '<':
            buf_indent_line(b, b->cy, -1);
            break;
    }
}

static void print_usage(void) {
    printf("catingiran (cti) v%s - Terminal IDE & Code Editor\n"
           "Usage: cti [options] [file|dir ...]\n"
           "Options:\n"
           "  -h, --help      Show this help\n"
           "  -v, --version   Show version\n",
           CTI_VERSION);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("catingiran (cti) %s\n", CTI_VERSION);
            return 0;
        }
    }

    memset(&E, 0, sizeof(E));
    config_load(&E.cfg);

    E.sidebar_visible = E.cfg.sidebar_open_on_start;
    E.focus = FOCUS_EDITOR;
    E.mode = MODE_NORMAL;
    E.running = 1;

    buf_init_empty(&E.buffers[0]);
    E.num_buffers = 1;
    E.cur_buf = 0;

    const char *tree_root = ".";
    int opened_any_file = 0;

    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            tree_root = argv[i];
            E.sidebar_visible = 1;
            E.focus = FOCUS_SIDEBAR;
        } else {
            buf_open(argv[i]);
            opened_any_file = 1;
        }
    }

    if (!opened_any_file && argc > 1) {
        /* Opened a directory */
        E.focus = FOCUS_SIDEBAR;
    }

    tree_init(tree_root);
    term_enable_raw();
    term_update_size();
    editor_set_status("catingiran v%s | :help for commands | Ctrl+B sidebar", CTI_VERSION);

    int need_render = 1;
    int prev_rows = E.screen_rows, prev_cols = E.screen_cols;
    while (E.running) {
        if (need_render || E.screen_rows != prev_rows || E.screen_cols != prev_cols) {
            prev_rows = E.screen_rows;
            prev_cols = E.screen_cols;
            ui_render();
            need_render = 0;
        }

        int ulen = 0;
        char ubuf[5];
        int key = term_read_key(&ulen, ubuf);
        if (key == KEY_NULL) continue;

        need_render = 1;

        if (key == KEY_MOUSE) {
            handle_mouse();
            continue;
        }

        if (E.mode == MODE_COMMAND) {
            handle_command_key(key, ulen, ubuf);
        } else if (E.mode == MODE_SEARCH) {
            handle_search_key(key, ulen, ubuf);
        } else if (E.mode == MODE_INSERT) {
            handle_insert_key(key, ulen, ubuf);
        } else if (E.mode == MODE_VISUAL) {
            handle_visual_key(key, ubuf);
        } else if (E.focus == FOCUS_SIDEBAR) {
            handle_sidebar_key(key, ubuf);
        } else {
            handle_normal_key(key, ubuf);
        }
    }

    term_disable_raw();
    for (int i = 0; i < E.num_buffers; i++) buf_free(&E.buffers[i]);
    tree_free();
    free(E.clipboard);
    return 0;
}
