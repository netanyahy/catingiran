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

static int take_count(void) {
    int n = E.count_prefix > 0 ? E.count_prefix : 1;
    E.count_prefix = 0;
    if (n > 10000) n = 10000;
    return n;
}

static int is_word_byte(unsigned char c) {
    return isalnum(c) || c == '_';
}

static void yank_range_to_clip(Buffer *b, int sr, int sc, int er, int ec) {
    if (b->num_lines <= 0) return;
    if (sr > er || (sr == er && sc > ec)) {
        int tr = sr, tc = sc; sr = er; sc = ec; er = tr; ec = tc;
    }
    if (sr < 0) sr = 0;
    if (er >= b->num_lines) er = b->num_lines - 1;
    if (sc < 0) sc = 0;
    if (sc > b->lines[sr].size) sc = b->lines[sr].size;
    if (ec < 0) ec = 0;
    if (ec > b->lines[er].size) ec = b->lines[er].size;

    Abuf ab = {NULL, 0, 0};
    if (sr == er) {
        if (ec > sc) ab_append(&ab, b->lines[sr].chars + sc, ec - sc);
    } else {
        ab_append(&ab, b->lines[sr].chars + sc, b->lines[sr].size - sc);
        ab_append_str(&ab, "\n");
        for (int r = sr + 1; r < er; r++) {
            ab_append(&ab, b->lines[r].chars, b->lines[r].size);
            ab_append_str(&ab, "\n");
        }
        ab_append(&ab, b->lines[er].chars, ec);
    }
    ab_append(&ab, "\0", 1);
    free(E.clipboard);
    E.clipboard = ab.b;
}

static void yank_lines_to_clip(Buffer *b, int start_row, int cnt) {
    if (start_row < 0 || start_row >= b->num_lines) return;
    int end_row = start_row + cnt - 1;
    if (end_row >= b->num_lines) end_row = b->num_lines - 1;
    Abuf ab = {NULL, 0, 0};
    for (int r = start_row; r <= end_row; r++) {
        if (b->lines[r].size > 0) ab_append(&ab, b->lines[r].chars, b->lines[r].size);
        ab_append_str(&ab, "\n");
    }
    ab_append(&ab, "\0", 1);
    free(E.clipboard);
    E.clipboard = ab.b;
    editor_set_status("%d line(s) yanked", end_row - start_row + 1);
}

static int find_text_object(Buffer *b, char mod, int obj,
                            int *sr, int *sc, int *er, int *ec) {
    if (!b || b->cy < 0 || b->cy >= b->num_lines) return 0;
    Line *ln = &b->lines[b->cy];

    if (obj == 'w') {
        if (ln->size == 0) return 0;
        int pos = b->cx < ln->size ? b->cx : ln->size - 1;
        int s = pos, e = pos + 1;
        if (is_word_byte((unsigned char)ln->chars[pos])) {
            while (s > 0 && is_word_byte((unsigned char)ln->chars[s - 1])) s--;
            while (e < ln->size && is_word_byte((unsigned char)ln->chars[e])) e++;
        } else if (isspace((unsigned char)ln->chars[pos])) {
            while (s > 0 && isspace((unsigned char)ln->chars[s - 1])) s--;
            while (e < ln->size && isspace((unsigned char)ln->chars[e])) e++;
        }
        if (mod == 'a') {
            int oe = e;
            while (e < ln->size && (ln->chars[e] == ' ' || ln->chars[e] == '\t')) e++;
            if (e == oe) {
                while (s > 0 && (ln->chars[s - 1] == ' ' || ln->chars[s - 1] == '\t')) s--;
            }
        }
        *sr = *er = b->cy;
        *sc = s;
        *ec = e;
        return 1;
    }

    if (obj == '"' || obj == '\'' || obj == '`') {
        int q = obj;
        int best_o = -1, best_c = -1;
        int cur_o = -1;
        for (int i = 0; i < ln->size; i++) {
            if (ln->chars[i] == '\\') { i++; continue; }
            if (ln->chars[i] == q) {
                if (cur_o == -1) {
                    cur_o = i;
                } else {
                    if ((cur_o <= b->cx && b->cx <= i) || (best_o == -1 && cur_o >= b->cx)) {
                        best_o = cur_o;
                        best_c = i;
                        break;
                    }
                    cur_o = -1;
                }
            }
        }
        if (best_o >= 0 && best_c > best_o) {
            *sr = *er = b->cy;
            *sc = (mod == 'i') ? (best_o + 1) : best_o;
            *ec = (mod == 'i') ? best_c : (best_c + 1);
            return 1;
        }
        return 0;
    }

    char open_ch = 0, close_ch = 0;
    if (obj == '(' || obj == ')' || obj == 'b') { open_ch = '('; close_ch = ')'; }
    else if (obj == '{' || obj == '}' || obj == 'B') { open_ch = '{'; close_ch = '}'; }
    else if (obj == '[' || obj == ']') { open_ch = '['; close_ch = ']'; }
    else if (obj == '<' || obj == '>') { open_ch = '<'; close_ch = '>'; }
    if (!open_ch) return 0;

    int min_r = b->cy - 200; if (min_r < 0) min_r = 0;
    int max_r = b->cy + 200; if (max_r >= b->num_lines) max_r = b->num_lines - 1;

    int orow = -1, ocol = -1;
    if (b->cx < ln->size && ln->chars[b->cx] == open_ch) {
        orow = b->cy;
        ocol = b->cx;
    } else {
        int depth = 0;
        for (int r = b->cy; r >= min_r && orow == -1; r--) {
            Line *rl = &b->lines[r];
            int c0 = (r == b->cy) ? (b->cx < rl->size ? b->cx : rl->size - 1) : (rl->size - 1);
            for (int c = c0; c >= 0; c--) {
                if (r == b->cy && c == b->cx && rl->chars[c] == close_ch) {
                    depth++;
                    continue;
                }
                if (rl->chars[c] == close_ch) depth++;
                else if (rl->chars[c] == open_ch) {
                    if (depth == 0) { orow = r; ocol = c; break; }
                    depth--;
                }
            }
        }
        if (orow == -1) {
            for (int c = b->cx; c < ln->size; c++) {
                if (ln->chars[c] == open_ch) { orow = b->cy; ocol = c; break; }
            }
        }
    }
    if (orow == -1) return 0;

    int crow = -1, ccol = -1, depth = 0;
    for (int r = orow; r <= max_r && crow == -1; r++) {
        Line *rl = &b->lines[r];
        int c0 = (r == orow) ? (ocol + 1) : 0;
        for (int c = c0; c < rl->size; c++) {
            if (rl->chars[c] == open_ch) depth++;
            else if (rl->chars[c] == close_ch) {
                if (depth == 0) { crow = r; ccol = c; break; }
                depth--;
            }
        }
    }
    if (crow == -1) return 0;

    if (mod == 'a') {
        *sr = orow; *sc = ocol;
        *er = crow; *ec = ccol + 1;
    } else {
        *sr = orow; *sc = ocol + 1;
        *er = crow; *ec = ccol;
    }
    return 1;
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
    int sb_start = (sb_w > 0) ? (E.cfg.sidebar_right ? (E.screen_cols - sb_w + 1) : 1) : 0;
    int sb_end = (sb_w > 0) ? (sb_start + sb_w - 1) : 0;
    int in_sb = (sb_w > 0 && mx >= sb_start && mx <= sb_end);
    int ed_start = E.cfg.sidebar_right ? 1 : (sb_w > 0 ? sb_w + 2 : 1);
    int ed_end = E.cfg.sidebar_right ? (sb_w > 0 ? sb_start - 2 : E.screen_cols) : E.screen_cols;
    int in_ed = (mx >= ed_start && mx <= ed_end);

    Buffer *b = &E.buffers[E.cur_buf];

    /* Scroll wheel */
    if (btn == 64 || btn == 65) {
        int dir = (btn == 64) ? -3 : 3;
        if (in_sb) {
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

    int status_rows = E.cfg.statusline ? 1 : 0;
    int out_h = (E.output_visible && E.screen_rows > 12) ? E.cfg.output_height : 0;
    int max_body_y = E.screen_rows - status_rows - 1 - out_h;
    if (my <= top_rows || my > max_body_y) return;

    /* Click in sidebar */
    if (in_sb) {
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
    if (in_ed) {
        E.focus = FOCUS_EDITOR;
        int gutter_w = E.cfg.line_numbers ? 6 : 1;
        int target_y = b->rowoff + (my - top_rows - 1);
        if (target_y < 0) target_y = 0;
        if (target_y >= b->num_lines) target_y = b->num_lines - 1;
        b->cy = target_y;

        int target_rx = b->coloff + (mx - (ed_start + gutter_w));
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

    if (key == KEY_CTRL_SPACE) {
        lsp_request_completion(E.cur_buf);
        ac_refresh(b, 1);
        return;
    }

    if (key == KEY_ESC) {
        if (E.ac_visible) {
            ac_dismiss();
            return;
        }
        E.mode = MODE_NORMAL;
        if (b->cx > 0) b->cx = utf8_prev_idx(b->lines[b->cy].chars, b->cx);
        clamp_cursor(b);
        return;
    }

    if (E.ac_visible) {
        if (key == KEY_ARROW_DOWN || key == KEY_TAB) {
            ac_move(1);
            return;
        }
        if (key == KEY_ARROW_UP || key == KEY_SHIFT_TAB) {
            ac_move(-1);
            return;
        }
        if (key == KEY_ENTER) {
            ac_accept(b);
            return;
        }
    }

    switch (key) {
        case KEY_ENTER:
            ac_dismiss();
            buf_insert_newline(b);
            break;
        case KEY_BACKSPACE:
            buf_backspace(b);
            ac_refresh(b, 0);
            break;
        case KEY_DEL:
            ac_dismiss();
            buf_delete_at_cursor(b);
            break;
        case KEY_TAB:
            /* If cursor is right after an identifier, open autocomplete menu */
            if (b->cx > 0 && (isalnum((unsigned char)b->lines[b->cy].chars[b->cx - 1]) ||
                              b->lines[b->cy].chars[b->cx - 1] == '_' ||
                              b->lines[b->cy].chars[b->cx - 1] == '#' ||
                              b->lines[b->cy].chars[b->cx - 1] == '@')) {
                ac_refresh(b, 1);
                if (E.ac_visible) break;
            }
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
            ac_dismiss();
            move_cursor(b, KEY_ARROW_LEFT);
            break;
        case CTRL_KEY('j'):
        case KEY_ARROW_DOWN:
            ac_dismiss();
            move_cursor(b, KEY_ARROW_DOWN);
            break;
        case CTRL_KEY('k'):
        case KEY_ARROW_UP:
            ac_dismiss();
            move_cursor(b, KEY_ARROW_UP);
            break;
        case CTRL_KEY('l'):
        case KEY_ARROW_RIGHT:
            ac_dismiss();
            move_cursor(b, KEY_ARROW_RIGHT);
            break;
        case KEY_HOME:
        case KEY_END:
        case KEY_PAGE_UP:
        case KEY_PAGE_DOWN:
            ac_dismiss();
            move_cursor(b, key);
            break;
        default:
            if (ulen > 0 && (unsigned char)ubuf[0] >= 32 && key < 1000) {
                if (E.cfg.auto_pairs && ulen == 1) {
                    Line *cur_ln = &b->lines[b->cy];
                    char ch = ubuf[0];
                    if ((ch == ')' || ch == ']' || ch == '}' || ch == '"' || ch == '\'') &&
                        b->cx < cur_ln->size && cur_ln->chars[b->cx] == ch) {
                        b->cx++;
                        break;
                    }
                    const char *pair = NULL;
                    switch (ch) {
                        case '(': pair = "()"; break;
                        case '[': pair = "[]"; break;
                        case '{': pair = "{}"; break;
                        case '"': pair = "\"\""; break;
                        case '\'': pair = "''"; break;
                    }
                    if (pair) {
                        ac_dismiss();
                        buf_insert_bytes(b, pair, 2);
                        b->cx--;
                        break;
                    }
                }
                buf_insert_bytes(b, ubuf, ulen);
                if (ulen == 1 && (isalnum((unsigned char)ubuf[0]) || ubuf[0] == '_' ||
                                  ubuf[0] == '.' || ubuf[0] == '>' || ubuf[0] == ':')) {
                    lsp_request_completion(E.cur_buf);
                }
                ac_refresh(b, 0);
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

    int sy = b->vis_sy, sx = b->vis_sx;
    int ey = b->cy, ex = b->cx;
    if (sy > ey || (sy == ey && sx > ex)) {
        int ty = sy, tx = sx;
        sy = ey; sx = ex;
        ey = ty; ex = tx;
    }

    if (E.pending_op == 'g') {
        E.pending_op = 0;
        if (key == 'c') {
            tools_toggle_comment(b, sy, ey);
            E.mode = MODE_NORMAL;
        }
        return;
    }

    if (E.pending_obj_mod) {
        char mod = E.pending_obj_mod;
        E.pending_obj_mod = 0;
        int osr = 0, osc = 0, oer = 0, oec = 0;
        if (find_text_object(b, mod, key, &osr, &osc, &oer, &oec)) {
            b->vis_sy = osr;
            b->vis_sx = osc;
            b->cy = oer;
            if (oec > 0 && oer < b->num_lines) {
                b->cx = utf8_prev_idx(b->lines[oer].chars, oec);
            } else {
                b->cx = 0;
            }
            clamp_cursor(b);
        }
        return;
    }

    switch (key) {
        case KEY_ESC:
        case 'v':
            E.pending_op = 0;
            E.pending_obj_mod = 0;
            E.mode = MODE_NORMAL;
            break;
        case 'g':
            E.pending_op = 'g';
            break;
        case 'i':
        case 'a':
            E.pending_obj_mod = (char)key;
            break;
        case 'J':
            tools_move_lines(b, sy, ey, 1);
            break;
        case 'K':
            tools_move_lines(b, sy, ey, -1);
            break;
        case 'h': case 'j': case 'k': case 'l':
        case KEY_ARROW_LEFT: case KEY_ARROW_DOWN: case KEY_ARROW_UP: case KEY_ARROW_RIGHT:
        case '0': case '$':
            move_cursor(b, key);
            break;
        case 'w':
            move_word_forward(b);
            break;
        case 'b':
            move_word_backward(b);
            break;
        case 'y': {
            Line *eln = &b->lines[ey];
            int ec = (ex < eln->size) ? utf8_next_idx(eln->chars, eln->size, ex) : eln->size;
            yank_range_to_clip(b, sy, sx, ey, ec);
            E.mode = MODE_NORMAL;
            editor_set_status("Selection yanked");
            break;
        }
        case 'd':
        case 'x':
        case 'c': {
            Line *eln = &b->lines[ey];
            if (sy == ey && eln->size == 0) {
                buf_delete_line(b, sy);
            } else {
                int ec = (ex < eln->size) ? utf8_next_idx(eln->chars, eln->size, ex) : eln->size;
                buf_delete_range(b, sy, sx, ey, ec, 1);
            }
            E.mode = (key == 'c') ? MODE_INSERT : MODE_NORMAL;
            if (E.mode != MODE_INSERT) clamp_cursor(b);
            break;
        }
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

static void dispatch_key(int key, int ulen, const char *ubuf);

static void handle_normal_key(int key, const char *ubuf) {
    Buffer *b = &E.buffers[E.cur_buf];

    if (key >= 128 && key < 1000 && E.cfg.russian_keymap) {
        int mapped = map_russian_normal(ubuf);
        if (mapped) key = mapped;
    }

    if (E.hover_visible) {
        E.hover_visible = 0;
        if (key == KEY_ESC) return;
    }

    /* <Space> f ... sequences (ff = fuzzy find, fg = grep, fm = format) */
    if (E.pending_f) {
        E.pending_f = 0;
        E.count_prefix = 0;
        if (key == 'f') {
            tools_fuzzy_find();
        } else if (key == 'g') {
            char q[128] = {0};
            editor_prompt_input("Grep project: ", q, sizeof(q));
            if (q[0]) tools_grep(q);
        } else if (key == 'm') {
            tools_format_buffer(b);
        }
        return;
    }

    /* <Space> r ... sequences (rn = LSP rename, rr = run current file) */
    if (E.pending_r) {
        E.pending_r = 0;
        E.count_prefix = 0;
        if (key == 'n') {
            char nn[128] = {0};
            editor_prompt_input("Rename symbol to: ", nn, sizeof(nn));
            if (nn[0]) lsp_request_rename(E.cur_buf, nn);
        } else if (key == 'r' || key == KEY_ENTER) {
            tools_run_current();
        }
        return;
    }

    /* Leader sequences (<Space> + ...) */
    if (E.pending_leader) {
        E.pending_leader = 0;
        E.count_prefix = 0;
        if (key == 'e') {
            E.sidebar_visible = !E.sidebar_visible;
            E.focus = E.sidebar_visible ? FOCUS_SIDEBAR : FOCUS_EDITOR;
            if (E.sidebar_visible) tree_refresh();
        } else if (key == 'f') {
            E.pending_f = 1;
        } else if (key == 'r') {
            E.pending_r = 1;
            editor_set_status("<Space>r: n=Rename symbol (LSP)  r=Run file");
        } else if (key == 't') {
            tools_spawn_shell();
        } else if (key == 'v') {
            cmd_execute("vsplit");
        } else if (key == 's') {
            cmd_execute("split");
        } else if (key == 'x') {
            cmd_execute("only");
        } else if (key == 'p') {
            plugins_status();
        } else if (key == 'g') {
            tools_git_status();
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

    if (E.pending_op == 'q') {
        E.pending_op = 0;
        E.count_prefix = 0;
        if (key >= 'a' && key <= 'z') {
            int reg = key - 'a';
            E.macro_recording = 1;
            E.macro_rec_reg = (char)key;
            E.macro_lens[reg] = 0;
            editor_set_status("Recording @%c", key);
        }
        return;
    }

    if (E.pending_op == '@') {
        E.pending_op = 0;
        int cnt = take_count();
        int reg = -1;
        if (key >= 'a' && key <= 'z') reg = key - 'a';
        else if (key == '@' && E.macro_rec_reg >= 'a' && E.macro_rec_reg <= 'z') {
            reg = E.macro_rec_reg - 'a';
        }
        if (reg >= 0 && E.macro_lens[reg] > 0 && E.macro_playing < 4) {
            E.macro_playing++;
            for (int rep = 0; rep < cnt && E.running; rep++) {
                for (int k = 0; k < E.macro_lens[reg] && E.running; k++) {
                    MacroKey *mk = &E.macros[reg][k];
                    dispatch_key(mk->key, mk->ulen, mk->ubuf);
                }
            }
            E.macro_playing--;
        }
        return;
    }

    /* Text object completion (e.g. c/d/y + i/a + w/"/(/{) */
    if (E.pending_obj_mod && (E.pending_op == 'd' || E.pending_op == 'c' || E.pending_op == 'y')) {
        char op = E.pending_op;
        char mod = E.pending_obj_mod;
        E.pending_op = 0;
        E.pending_obj_mod = 0;
        E.count_prefix = 0;
        int osr = 0, osc = 0, oer = 0, oec = 0;
        if (find_text_object(b, mod, key, &osr, &osc, &oer, &oec)) {
            if (op == 'y') {
                yank_range_to_clip(b, osr, osc, oer, oec);
                editor_set_status("Yanked");
            } else {
                buf_delete_range(b, osr, osc, oer, oec, 1);
                if (op == 'c') {
                    E.mode = MODE_INSERT;
                } else {
                    clamp_cursor(b);
                }
            }
        }
        return;
    }

    if (E.pending_op == 'C') {
        E.pending_op = 0;
        E.count_prefix = 0;
        if (key == 'c') tools_toggle_comment(b, b->cy, b->cy);
        return;
    }

    if (E.pending_op == 'g') {
        E.pending_op = 0;
        if (key == 'g') {
            int ln_no = (E.count_prefix > 0) ? take_count() : 1;
            if (ln_no < 1) ln_no = 1;
            if (ln_no > b->num_lines) ln_no = b->num_lines;
            b->cy = ln_no - 1;
            b->cx = 0;
            clamp_cursor(b);
        } else {
            E.count_prefix = 0;
            if (key == 'd') {
                lsp_request_definition(E.cur_buf);
            } else if (key == 'r') {
                lsp_request_references(E.cur_buf);
            } else if (key == 'c') {
                E.pending_op = 'C';
            } else if (key == 'f') {
                tools_jump_grep_line(b);
            }
        }
        return;
    }

    if (E.pending_op == ']') {
        E.pending_op = 0;
        E.count_prefix = 0;
        if (key == 'd') tools_next_diag(b, 1);
        return;
    }

    if (E.pending_op == '[') {
        E.pending_op = 0;
        E.count_prefix = 0;
        if (key == 'd') tools_next_diag(b, -1);
        return;
    }

    if (E.pending_op == '>' || E.pending_op == '<') {
        char op = E.pending_op;
        E.pending_op = 0;
        int cnt = take_count();
        if (key == op) {
            for (int k = 0; k < cnt && b->cy + k < b->num_lines; k++) {
                buf_indent_line(b, b->cy + k, op == '>' ? 1 : -1);
            }
        }
        return;
    }

    if (E.pending_op == 'd' || E.pending_op == 'c' || E.pending_op == 'y') {
        char op = E.pending_op;
        if ((key >= '1' && key <= '9') || (key == '0' && E.count_prefix > 0)) {
            if (E.count_prefix < 10000) E.count_prefix = E.count_prefix * 10 + (key - '0');
            return;
        }
        if (key == 'i' || key == 'a') {
            E.pending_obj_mod = (char)key;
            return;
        }
        E.pending_op = 0;
        int cnt = take_count();
        if (key == op) {
            if (op == 'd') {
                for (int k = 0; k < cnt; k++) buf_delete_line(b, b->cy);
                clamp_cursor(b);
            } else if (op == 'y') {
                yank_lines_to_clip(b, b->cy, cnt);
            } else if (op == 'c') {
                for (int k = 1; k < cnt && b->num_lines > 1; k++) buf_delete_line(b, b->cy);
                Line *ln = &b->lines[b->cy];
                int ind = 0;
                while (ind < ln->size && (ln->chars[ind] == ' ' || ln->chars[ind] == '\t')) ind++;
                if (ln->size > ind) buf_delete_range(b, b->cy, ind, b->cy, ln->size, 1);
                b->cx = ind;
                E.mode = MODE_INSERT;
            }
        } else if (key == 'w' || key == 'e') {
            int sy = b->cy, sx = b->cx;
            Line *ln = &b->lines[b->cy];
            int ex = sx;
            for (int k = 0; k < cnt; k++) {
                while (ex < ln->size && !isspace((unsigned char)ln->chars[ex])) ex++;
                if (key == 'w') {
                    while (ex < ln->size && isspace((unsigned char)ln->chars[ex])) ex++;
                }
            }
            if (op == 'y') {
                yank_range_to_clip(b, sy, sx, sy, ex);
                editor_set_status("Yanked");
            } else {
                buf_delete_range(b, sy, sx, sy, ex, 1);
                if (op == 'c') E.mode = MODE_INSERT;
                else clamp_cursor(b);
            }
        } else if (key == '$') {
            int ex = b->lines[b->cy].size;
            if (op == 'y') {
                yank_range_to_clip(b, b->cy, b->cx, b->cy, ex);
                editor_set_status("Yanked");
            } else {
                buf_delete_range(b, b->cy, b->cx, b->cy, ex, 1);
                if (op == 'c') E.mode = MODE_INSERT;
                else clamp_cursor(b);
            }
        }
        return;
    }

    if ((key >= '1' && key <= '9') || (key == '0' && E.count_prefix > 0)) {
        if (E.count_prefix < 10000) E.count_prefix = E.count_prefix * 10 + (key - '0');
        return;
    }

    switch (key) {
        case 'q':
            E.count_prefix = 0;
            if (E.macro_recording) {
                int reg = (E.macro_rec_reg >= 'a' && E.macro_rec_reg <= 'z')
                          ? (E.macro_rec_reg - 'a') : 0;
                E.macro_recording = 0;
                editor_set_status("Recorded @%c (%d keys)",
                                  E.macro_rec_reg ? E.macro_rec_reg : '?',
                                  E.macro_lens[reg]);
            } else {
                E.pending_op = 'q';
            }
            break;
        case '@':
            E.pending_op = '@';
            break;
        case ' ':
            E.count_prefix = 0;
            E.pending_leader = 1;
            break;
        case 'g':
            E.pending_op = 'g';
            break;
        case 'G': {
            if (E.count_prefix > 0) {
                int ln_no = take_count();
                if (ln_no < 1) ln_no = 1;
                if (ln_no > b->num_lines) ln_no = b->num_lines;
                b->cy = ln_no - 1;
            } else {
                b->cy = b->num_lines - 1;
            }
            b->cx = 0;
            clamp_cursor(b);
            break;
        }
        case 'd':
            E.pending_op = 'd';
            break;
        case 'c':
            E.pending_op = 'c';
            break;
        case 'y':
            E.pending_op = 'y';
            break;
        case 'D':
            E.count_prefix = 0;
            buf_delete_range(b, b->cy, b->cx, b->cy, b->lines[b->cy].size, 1);
            clamp_cursor(b);
            break;
        case 'C':
            E.count_prefix = 0;
            buf_delete_range(b, b->cy, b->cx, b->cy, b->lines[b->cy].size, 1);
            E.mode = MODE_INSERT;
            break;
        case 'p': {
            int cnt = take_count();
            for (int k = 0; k < cnt; k++) paste_clipboard(b);
            break;
        }
        case 'u': {
            int cnt = take_count();
            for (int k = 0; k < cnt; k++) buf_undo(b);
            clamp_cursor(b);
            break;
        }
        case CTRL_KEY('r'): {
            int cnt = take_count();
            for (int k = 0; k < cnt; k++) buf_redo(b);
            clamp_cursor(b);
            break;
        }
        case 'x': {
            int cnt = take_count();
            for (int k = 0; k < cnt; k++) buf_delete_at_cursor(b);
            clamp_cursor(b);
            break;
        }
        case 'K':
            E.count_prefix = 0;
            lsp_request_hover(E.cur_buf);
            break;
        case 'h': case 'j': case 'k': case 'l':
        case KEY_ARROW_LEFT: case KEY_ARROW_DOWN: case KEY_ARROW_UP: case KEY_ARROW_RIGHT:
        case '0': case '$': case KEY_HOME: case KEY_END:
        case KEY_PAGE_UP: case KEY_PAGE_DOWN:
        case CTRL_KEY('u'): case CTRL_KEY('d'): {
            int cnt = take_count();
            for (int k = 0; k < cnt; k++) move_cursor(b, key);
            break;
        }
        case 'w': {
            int cnt = take_count();
            for (int k = 0; k < cnt; k++) move_word_forward(b);
            break;
        }
        case 'b': {
            int cnt = take_count();
            for (int k = 0; k < cnt; k++) move_word_backward(b);
            break;
        }
        case 'i':
            E.count_prefix = 0;
            E.mode = MODE_INSERT;
            break;
        case 'I':
            E.count_prefix = 0;
            b->cx = 0;
            while (b->cx < b->lines[b->cy].size &&
                   (b->lines[b->cy].chars[b->cx] == ' ' || b->lines[b->cy].chars[b->cx] == '\t')) {
                b->cx++;
            }
            E.mode = MODE_INSERT;
            break;
        case 'a':
            E.count_prefix = 0;
            if (b->cx < b->lines[b->cy].size) {
                b->cx = utf8_next_idx(b->lines[b->cy].chars, b->lines[b->cy].size, b->cx);
            }
            E.mode = MODE_INSERT;
            break;
        case 'A':
            E.count_prefix = 0;
            b->cx = b->lines[b->cy].size;
            E.mode = MODE_INSERT;
            break;
        case 'o':
            E.count_prefix = 0;
            b->cx = b->lines[b->cy].size;
            buf_insert_newline(b);
            E.mode = MODE_INSERT;
            break;
        case 'O': {
            E.count_prefix = 0;
            b->cx = 0;
            buf_insert_newline(b);
            b->cy--;
            b->cx = 0;
            E.mode = MODE_INSERT;
            break;
        }
        case 'v':
            E.count_prefix = 0;
            b->vis_sx = b->cx;
            b->vis_sy = b->cy;
            E.mode = MODE_VISUAL;
            break;
        case ':':
            E.count_prefix = 0;
            E.mode = MODE_COMMAND;
            E.cmd_len = 0;
            E.cmd_pos = 0;
            E.cmd_buf[0] = '\0';
            E.cmd_hist_idx = E.cmd_hist_len;
            break;
        case '/':
            E.count_prefix = 0;
            E.mode = MODE_SEARCH;
            E.search_len = 0;
            E.search_buf[0] = '\0';
            break;
        case 'n':
            E.count_prefix = 0;
            if (E.search_len > 0) buf_find(b, E.search_buf, 1);
            break;
        case 'N':
            E.count_prefix = 0;
            if (E.search_len > 0) buf_find(b, E.search_buf, -1);
            break;
        case KEY_ESC:
            E.count_prefix = 0;
            E.pending_op = 0;
            E.pending_obj_mod = 0;
            E.search_len = 0;
            E.search_buf[0] = '\0';
            break;
        case CTRL_KEY('b'):
        case KEY_F2:
            E.count_prefix = 0;
            E.sidebar_visible = !E.sidebar_visible;
            E.focus = E.sidebar_visible ? FOCUS_SIDEBAR : FOCUS_EDITOR;
            if (E.sidebar_visible) tree_refresh();
            break;
        case KEY_F5:
            E.count_prefix = 0;
            tools_run_current();
            break;
        case CTRL_KEY('p'):
            E.count_prefix = 0;
            tools_fuzzy_find();
            break;
        case CTRL_KEY('j'):
            E.count_prefix = 0;
            tools_toggle_output();
            break;
        case CTRL_KEY('w'):
            E.count_prefix = 0;
            if (E.split_mode != SPLIT_NONE) {
                int tmp = E.cur_buf;
                E.cur_buf = E.split_buf;
                E.split_buf = tmp;
                E.active_pane = !E.active_pane;
            } else if (E.sidebar_visible) {
                E.focus = FOCUS_SIDEBAR;
            }
            break;
        case 'm':
            E.count_prefix = 0;
            tools_toggle_bookmark(b, b->cy);
            break;
        case 'M':
            E.count_prefix = 0;
            tools_next_bookmark(b);
            break;
        case KEY_ENTER:
            E.count_prefix = 0;
            if (b->filename && strcmp(b->filename, "[Grep]") == 0) {
                tools_jump_grep_line(b);
            }
            break;
        case CTRL_KEY('h'):
            E.count_prefix = 0;
            if (E.sidebar_visible) E.focus = FOCUS_SIDEBAR;
            break;
        case CTRL_KEY('l'):
            E.count_prefix = 0;
            E.focus = FOCUS_EDITOR;
            break;
        case KEY_TAB:
            E.count_prefix = 0;
            E.cur_buf = (E.cur_buf + 1) % E.num_buffers;
            break;
        case KEY_SHIFT_TAB:
            E.count_prefix = 0;
            E.cur_buf = (E.cur_buf - 1 + E.num_buffers) % E.num_buffers;
            break;
        case ']':
            E.pending_op = ']';
            break;
        case '[':
            E.pending_op = '[';
            break;
        case '>':
            E.pending_op = '>';
            break;
        case '<':
            E.pending_op = '<';
            break;
        default:
            E.count_prefix = 0;
            break;
    }
}

static void dispatch_key(int key, int ulen, const char *ubuf) {
    if (key == KEY_MOUSE) {
        handle_mouse();
        return;
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

static int is_dir_path(const char *p) {
    if (!p || !p[0]) return 0;
    struct stat st;
    return (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
}

static void extract_parent_dir(const char *filepath, char *out, size_t out_sz) {
    if (!filepath || !filepath[0]) {
        snprintf(out, out_sz, ".");
        return;
    }
    const char *sl = strrchr(filepath, '/');
    if (!sl) {
        snprintf(out, out_sz, ".");
    } else if (sl == filepath) {
        snprintf(out, out_sz, "/");
    } else {
        size_t len = (size_t)(sl - filepath);
        if (len >= out_sz) len = out_sz - 1;
        memcpy(out, filepath, len);
        out[len] = '\0';
    }
}

static void print_usage(void) {
    printf("catingiran (cti) v%s - Terminal IDE & Code Editor\n"
           "Usage: cti [options] [file|dir ...]\n"
           "Options:\n"
           "  -ot, --tree [dir]   Open file explorer sidebar (optionally in dir)\n"
           "  -ht, --no-tree      Keep file explorer sidebar closed on startup\n"
           "  -h, --help          Show this help\n"
           "  -v, --version       Show version\n",
           CTI_VERSION);
}

int main(int argc, char **argv) {
    int tree_flag = 0; /* 0 = auto, 1 = force open (-ot), -1 = force hide (-ht) */
    const char *explicit_dir = NULL;
    const char **file_args = malloc((size_t)(argc > 0 ? argc : 1) * sizeof(char *));
    int n_files = 0;
    int stop_opts = 0;

    for (int i = 1; i < argc; i++) {
        if (!stop_opts && strcmp(argv[i], "--") == 0) {
            stop_opts = 1;
            continue;
        }
        if (!stop_opts) {
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                free(file_args);
                print_usage();
                return 0;
            }
            if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
                free(file_args);
                printf("catingiran (cti) %s\n", CTI_VERSION);
                return 0;
            }
            if (strcmp(argv[i], "-ot") == 0 || strcmp(argv[i], "--tree") == 0) {
                tree_flag = 1;
                if (i + 1 < argc && is_dir_path(argv[i + 1])) {
                    explicit_dir = argv[++i];
                }
                continue;
            }
            if (strncmp(argv[i], "-ot=", 4) == 0 || strncmp(argv[i], "--tree=", 7) == 0) {
                const char *d = (argv[i][1] == 'o') ? (argv[i] + 4) : (argv[i] + 7);
                tree_flag = 1;
                if (d[0]) explicit_dir = d;
                continue;
            }
            if (strcmp(argv[i], "-ht") == 0 || strcmp(argv[i], "--no-tree") == 0) {
                tree_flag = -1;
                continue;
            }
        }
        if (is_dir_path(argv[i])) {
            if (!explicit_dir) explicit_dir = argv[i];
        } else if (file_args) {
            file_args[n_files++] = argv[i];
        }
    }

    memset(&E, 0, sizeof(E));
    config_load(&E.cfg);

    E.mode = MODE_NORMAL;
    E.running = 1;

    buf_init_empty(&E.buffers[0]);
    E.num_buffers = 1;
    E.cur_buf = 0;

    char tree_root[1024];
    if (explicit_dir && is_dir_path(explicit_dir)) {
        snprintf(tree_root, sizeof(tree_root), "%s", explicit_dir);
    } else if (n_files > 0) {
        extract_parent_dir(file_args[0], tree_root, sizeof(tree_root));
    } else {
        snprintf(tree_root, sizeof(tree_root), ".");
    }

    if (tree_flag == 1) {
        E.sidebar_visible = 1;
    } else if (tree_flag == -1) {
        E.sidebar_visible = 0;
    } else if (explicit_dir) {
        E.sidebar_visible = 1;
    } else if (n_files > 0) {
        E.sidebar_visible = 0;
    } else {
        E.sidebar_visible = E.cfg.sidebar_open_on_start;
    }

    E.focus = (E.sidebar_visible && n_files == 0 && (explicit_dir || tree_flag == 1))
              ? FOCUS_SIDEBAR : FOCUS_EDITOR;

    tree_init(tree_root);
    tools_refresh_git_branch();
    lsp_init_all();

    for (int i = 0; i < n_files; i++) {
        buf_open(file_args[i]);
    }
    free(file_args);

    term_enable_raw();
    term_update_size();
    editor_set_status("catingiran v%s | :help | :conf | Ctrl+B sidebar | gd/K LSP", CTI_VERSION);

    int need_render = 1;
    int prev_rows = E.screen_rows, prev_cols = E.screen_cols;
    while (E.running) {
        if (lsp_poll_events()) need_render = 1;
        if (need_render || E.screen_rows != prev_rows || E.screen_cols != prev_cols) {
            prev_rows = E.screen_rows;
            prev_cols = E.screen_cols;
            ui_render();
            need_render = 0;
        }

        int wait_res = term_wait_input(50);
        if (lsp_poll_events()) need_render = 1;
        if (wait_res == 0) continue;
        if (wait_res == 2) {
            need_render = 1;
            continue;
        }

        int ulen = 0;
        char ubuf[5];
        int key = term_read_key(&ulen, ubuf);
        if (key == KEY_NULL) continue;

        need_render = 1;

        if (E.macro_recording && !E.macro_playing) {
            int stop_rec = (E.mode == MODE_NORMAL && E.focus == FOCUS_EDITOR &&
                            !E.pending_op && !E.pending_leader &&
                            !E.pending_f && !E.pending_r && key == 'q');
            if (!stop_rec && E.macro_rec_reg >= 'a' && E.macro_rec_reg <= 'z') {
                int reg = E.macro_rec_reg - 'a';
                if (E.macro_lens[reg] < MAX_MACRO_KEYS) {
                    MacroKey *mk = &E.macros[reg][E.macro_lens[reg]++];
                    mk->key = key;
                    mk->ulen = ulen;
                    memcpy(mk->ubuf, ubuf, 5);
                }
            }
        }

        dispatch_key(key, ulen, ubuf);
    }

    lsp_shutdown_all();
    term_disable_raw();
    for (int i = 0; i < E.num_buffers; i++) buf_free(&E.buffers[i]);
    tree_free();
    free(E.clipboard);
    return 0;
}
