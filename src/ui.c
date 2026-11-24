#include "state.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void append_spaces(Abuf *ab, int count) {
    for (int i = 0; i < count; i++) ab_append(ab, " ", 1);
}

/* Append UTF-8 string truncated to max_cols display width, return actual width drawn */
static int ab_append_utf8_cols(Abuf *ab, const char *s, int bytes, int max_cols) {
    int col = 0;
    int i = 0;
    while (i < bytes && col < max_cols) {
        unsigned char c = (unsigned char)s[i];
        int clen = utf8_char_len(c);
        if (i + clen > bytes) break;
        int w = (clen == 4) ? 2 : 1;
        if (col + w > max_cols) break;
        ab_append(ab, s + i, clen);
        col += w;
        i += clen;
    }
    return col;
}

static const char *file_icon(const TreeNode *n) {
    if (!E.cfg.nerd_icons) {
        if (n->is_dir) return n->expanded ? "v " : "> ";
        return "  ";
    }
    if (n->is_dir) return n->expanded ? "▾  " : "▸  ";

    const char *dot = strrchr(n->name, '.');
    if (strcmp(n->name, "Makefile") == 0) return "   ";
    if (!dot) return "   ";
    if (strcmp(dot, ".c") == 0) return "   ";
    if (strcmp(dot, ".h") == 0) return "   ";
    if (strcmp(dot, ".py") == 0) return "   ";
    if (strcmp(dot, ".java") == 0) return "   ";
    if (strcmp(dot, ".js") == 0 || strcmp(dot, ".ts") == 0) return "   ";
    if (strcmp(dot, ".sh") == 0 || strcmp(dot, ".mksh") == 0) return "   ";
    if (strcmp(dot, ".md") == 0) return "   ";
    if (strcmp(dot, ".conf") == 0 || strcmp(dot, ".ini") == 0) return "   ";
    return "   ";
}

static const char *base_name(const char *path) {
    if (!path || !path[0]) return "[No Name]";
    const char *sl = strrchr(path, '/');
    return sl ? sl + 1 : path;
}

static void scroll_editor(Buffer *b, int ed_rows, int ed_cols) {
    int gutter_w = E.cfg.line_numbers ? 6 : 1;
    int text_cols = ed_cols - gutter_w;
    if (text_cols < 1) text_cols = 1;

    if (b->cy < b->rowoff) b->rowoff = b->cy;
    if (b->cy >= b->rowoff + ed_rows) b->rowoff = b->cy - ed_rows + 1;

    int rx = buf_cx_to_rx(b, b->cy, b->cx);
    if (rx < b->coloff) b->coloff = rx;
    if (rx >= b->coloff + text_cols) b->coloff = rx - text_cols + 1;
}

static void scroll_sidebar(int sb_rows) {
    int list_rows = sb_rows - 1; /* 1 row for header */
    if (list_rows < 1) list_rows = 1;
    if (E.tree_sel < E.tree_scroll) E.tree_scroll = E.tree_sel;
    if (E.tree_sel >= E.tree_scroll + list_rows) {
        E.tree_scroll = E.tree_sel - list_rows + 1;
    }
    if (E.tree_scroll < 0) E.tree_scroll = 0;
}

static void draw_tabline(Abuf *ab) {
    ab_append_bg(ab, E.theme.tab_bg);
    ab_append_fg(ab, E.theme.tab_fg);

    int col = 0;
    for (int i = 0; i < E.num_buffers && col < E.screen_cols - 14; i++) {
        Buffer *b = &E.buffers[i];
        int active = (i == E.cur_buf);

        if (active) {
            ab_append_bg(ab, E.theme.tab_active_bg);
            ab_append_fg(ab, E.theme.tab_active_fg);
            ab_append_str(ab, "\x1b[1m");
        } else {
            ab_append_bg(ab, E.theme.tab_bg);
            ab_append_fg(ab, E.theme.tab_fg);
            ab_append_str(ab, "\x1b[22m");
        }

        char label[128];
        snprintf(label, sizeof(label), " %d %s%s ", i + 1, base_name(b->filename),
                 b->dirty ? "*" : "");
        int rem = E.screen_cols - col - 14;
        if (rem <= 0) break;
        col += ab_append_utf8_cols(ab, label, (int)strlen(label), rem);
    }

    ab_append_bg(ab, E.theme.tab_bg);
    ab_append_fg(ab, E.theme.tab_fg);
    ab_append_str(ab, "\x1b[22m");

    const char *brand = " catingiran ";
    int blen = (int)strlen(brand);
    int pad = E.screen_cols - col - blen;
    if (pad > 0) append_spaces(ab, pad);
    if (col + blen <= E.screen_cols) {
        ab_append_str(ab, brand);
    }
}

static int in_visual_range(Buffer *b, int row, int byte_col) {
    if (E.mode != MODE_VISUAL) return 0;
    int sy = b->vis_sy, sx = b->vis_sx;
    int ey = b->cy, ex = b->cx;
    if (sy > ey || (sy == ey && sx > ex)) {
        int ty = sy; sy = ey; ey = ty;
        int tx = sx; sx = ex; ex = tx;
    }
    if (row < sy || row > ey) return 0;
    if (row == sy && row == ey) return byte_col >= sx && byte_col <= ex;
    if (row == sy) return byte_col >= sx;
    if (row == ey) return byte_col <= ex;
    return 1;
}

static void draw_sidebar_cell(Abuf *ab, int rel_row, int sb_w) {
    ab_append_bg(ab, E.theme.sidebar_bg);
    if (rel_row == 0) {
        ab_append_fg(ab, E.theme.sidebar_dir_fg);
        ab_append_str(ab, "\x1b[1m");
        char hdr[128];
        const char *root_base = base_name(E.root_dir);
        snprintf(hdr, sizeof(hdr), " %s%.64s",
                 E.cfg.nerd_icons ? " " : "EXPLORER: ", root_base);
        int w = ab_append_utf8_cols(ab, hdr, (int)strlen(hdr), sb_w);
        append_spaces(ab, sb_w - w);
        ab_append_str(ab, "\x1b[22m");
        return;
    }

    int idx = E.tree_scroll + (rel_row - 1);
    if (idx < 0 || idx >= E.tree_len) {
        append_spaces(ab, sb_w);
        return;
    }

    TreeNode *n = &E.tree[idx];
    int selected = (idx == E.tree_sel);

    if (selected) {
        ab_append_bg(ab, E.theme.sidebar_sel_bg);
        ab_append_fg(ab, E.theme.sidebar_sel_fg);
        if (E.focus == FOCUS_SIDEBAR) ab_append_str(ab, "\x1b[1m");
    } else {
        ab_append_bg(ab, E.theme.sidebar_bg);
        ab_append_fg(ab, n->is_dir ? E.theme.sidebar_dir_fg : E.theme.sidebar_fg);
    }

    int used = 0;
    int indent = n->depth * 2 + 1;
    if (indent > sb_w - 4) indent = sb_w - 4;
    append_spaces(ab, indent);
    used += indent;

    const char *icon = file_icon(n);
    used += ab_append_utf8_cols(ab, icon, (int)strlen(icon), sb_w - used);
    used += ab_append_utf8_cols(ab, n->name, (int)strlen(n->name), sb_w - used);
    if (used < sb_w) append_spaces(ab, sb_w - used);
    ab_append_str(ab, "\x1b[22m");
}

static void draw_welcome_line(Abuf *ab, int rel_row, int ed_rows, int ed_cols) {
    static const char *welcome[] = {
        "catingiran (cti) v" CTI_VERSION,
        "",
        "Ctrl+B / :ot / :ht  -  Toggle file explorer",
        "Ctrl+P / :find      -  Fuzzy find file in project",
        ":grep <text>        -  Project-wide search",
        "F5 / :run / Ctrl+J  -  Run code & toggle output drawer",
        ":vsplit / :split    -  Split panes (Ctrl+W switch)",
        ":conf / :PlugStatus -  Edit config / Plugin manager",
        ":t1..:t9 / :theme   -  Switch tabs / Switch theme"
    };
    int count = (int)(sizeof(welcome) / sizeof(welcome[0]));
    int start_row = ed_rows / 2 - count / 2;
    if (rel_row < start_row || rel_row >= start_row + count) {
        append_spaces(ab, ed_cols);
        return;
    }

    const char *msg = welcome[rel_row - start_row];
    int mlen = (int)strlen(msg);
    int pad = (ed_cols - mlen) / 2;
    if (pad < 0) pad = 0;
    append_spaces(ab, pad);
    ab_append_fg(ab, (rel_row == start_row) ? E.theme.fg : E.theme.gutter_fg);
    int w = ab_append_utf8_cols(ab, msg, mlen, ed_cols - pad);
    if (pad + w < ed_cols) append_spaces(ab, ed_cols - pad - w);
}

static void draw_editor_row(Abuf *ab, Buffer *b, int is_active_pane, int rel_row, int ed_rows, int ed_cols) {
    int file_row = b->rowoff + rel_row;
    int gutter_w = E.cfg.line_numbers ? 6 : 1;
    int text_cols = ed_cols - gutter_w;
    if (text_cols < 1) text_cols = 1;

    int is_cur_line = (is_active_pane && file_row == b->cy && E.cfg.cursorline && E.focus == FOCUS_EDITOR);
    Color line_bg = is_cur_line ? E.theme.cursorline_bg : E.theme.bg;

    if (file_row >= b->num_lines) {
        ab_append_bg(ab, E.theme.bg);
        ab_append_fg(ab, E.theme.gutter_fg);
        if (b->num_lines == 1 && b->lines[0].size == 0 && !b->filename && rel_row > 0 && is_active_pane) {
            ab_append_str(ab, "~");
            draw_welcome_line(ab, rel_row, ed_rows, ed_cols - 1);
        } else {
            ab_append_str(ab, "~");
            append_spaces(ab, ed_cols - 1);
        }
        return;
    }

    Line *ln = &b->lines[file_row];

    /* Gutter */
    ab_append_bg(ab, is_cur_line ? E.theme.cursorline_bg : E.theme.gutter_bg);
    if (ln->bookmarked) {
        ab_append_fg(ab, E.theme.hl_match);
    } else {
        ab_append_fg(ab, (is_active_pane && file_row == b->cy) ? E.theme.fg : E.theme.gutter_fg);
    }
    if (E.cfg.line_numbers) {
        char gbuf[16];
        snprintf(gbuf, sizeof(gbuf), "%4d%s", file_row + 1, ln->bookmarked ? "★ " : "  ");
        ab_append_str(ab, gbuf);
    } else {
        ab_append_str(ab, ln->bookmarked ? "★" : " ");
    }

    int slen = (E.search_len > 0) ? E.search_len : 0;

    int rx = 0;
    int drawn = 0;
    int i = 0;
    ab_append_bg(ab, line_bg);

    while (i < ln->size && drawn < text_cols) {
        unsigned char c = (unsigned char)ln->chars[i];
        int clen = utf8_char_len(c);
        if (i + clen > ln->size) break;

        int is_match = (slen > 0 && i + slen <= ln->size &&
                        strncmp(ln->chars + i, E.search_buf, (size_t)slen) == 0);
        int is_vis = in_visual_range(b, file_row, i);

        if (c == '\t') {
            int ts = E.cfg.tab_size > 0 ? E.cfg.tab_size : 4;
            int spaces = ts - (rx % ts);
            ab_append_bg(ab, is_vis ? E.theme.selection_bg : line_bg);
            for (int s = 0; s < spaces; s++) {
                if (rx >= b->coloff && drawn < text_cols) {
                    ab_append(ab, " ", 1);
                    drawn++;
                }
                rx++;
            }
            i++;
            continue;
        }

        int cw = (clen == 4) ? 2 : 1;
        if (rx + cw > b->coloff) {
            if (drawn + cw > text_cols) break;

            if (is_match) {
                ab_append_bg(ab, E.theme.hl_match);
                ab_append_fg(ab, E.theme.status_mode_fg);
            } else if (is_vis) {
                ab_append_bg(ab, E.theme.selection_bg);
                ab_append_fg(ab, E.theme.fg);
            } else {
                ab_append_bg(ab, line_bg);
                HlType hl = ln->hl ? (HlType)ln->hl[i] : HL_NORMAL;
                ab_append_fg(ab, syntax_color(hl));
            }
            ab_append(ab, ln->chars + i, clen);
            drawn += cw;
        }
        rx += cw;
        i += clen;
    }

    if (drawn < text_cols) {
        ab_append_bg(ab, line_bg);
        append_spaces(ab, text_cols - drawn);
    }
}

static void draw_statusline(Abuf *ab, Buffer *b) {
    const char *mode_str = " NORMAL ";
    if (E.focus == FOCUS_SIDEBAR && E.mode == MODE_NORMAL) {
        mode_str = " TREE ";
    } else if (E.mode == MODE_INSERT) {
        mode_str = " INSERT ";
    } else if (E.mode == MODE_VISUAL) {
        mode_str = " VISUAL ";
    } else if (E.mode == MODE_COMMAND) {
        mode_str = " COMMAND ";
    } else if (E.mode == MODE_SEARCH) {
        mode_str = " SEARCH ";
    }

    ab_append_bg(ab, E.theme.status_mode_bg);
    ab_append_fg(ab, E.theme.status_mode_fg);
    ab_append_str(ab, "\x1b[1m");
    ab_append_str(ab, mode_str);
    ab_append_str(ab, "\x1b[22m");

    ab_append_bg(ab, E.theme.status_bg);
    ab_append_fg(ab, E.theme.status_fg);

    char left[256];
    snprintf(left, sizeof(left), " %s%s%s%s",
             b->filename ? b->filename : "[No Name]",
             b->dirty ? " [+]" : "",
             E.git_branch[0] ? "   " : "",
             E.git_branch[0] ? E.git_branch : "");

    char right[160];
    int perc = b->num_lines > 0 ? ((b->cy + 1) * 100) / b->num_lines : 100;
    snprintf(right, sizeof(right), " %s | UTF-8 | %d:%d (%d%%) ",
             E.theme.name, b->cy + 1, buf_cx_to_rx(b, b->cy, b->cx) + 1, perc);

    int mode_w = (int)strlen(mode_str);
    int r_w = (int)strlen(right);
    int max_left = E.screen_cols - mode_w - r_w;
    if (max_left < 0) max_left = 0;

    int l_w = ab_append_utf8_cols(ab, left, (int)strlen(left), max_left);
    int pad = E.screen_cols - mode_w - l_w - r_w;
    if (pad > 0) append_spaces(ab, pad);
    if (mode_w + l_w + r_w <= E.screen_cols) {
        ab_append_str(ab, right);
    }
}

static void draw_cmdline(Abuf *ab) {
    ab_append_bg(ab, E.theme.bg);
    ab_append_fg(ab, E.theme.fg);

    int drawn = 0;
    if (E.mode == MODE_COMMAND) {
        ab_append_str(ab, ":");
        drawn = 1 + ab_append_utf8_cols(ab, E.cmd_buf, E.cmd_len, E.screen_cols - 1);
    } else if (E.mode == MODE_SEARCH) {
        ab_append_str(ab, "/");
        drawn = 1 + ab_append_utf8_cols(ab, E.search_buf, E.search_len, E.screen_cols - 1);
    } else if (E.status_msg[0] && time(NULL) - E.status_msg_time < 5) {
        drawn = ab_append_utf8_cols(ab, E.status_msg, (int)strlen(E.status_msg), E.screen_cols);
    }
    if (drawn < E.screen_cols) append_spaces(ab, E.screen_cols - drawn);
}

static void draw_output_row(Abuf *ab, int row_idx, int total_cols) {
    if (row_idx == 0) {
        ab_append_bg(ab, E.theme.tab_active_bg);
        ab_append_fg(ab, E.theme.tab_active_fg);
        char hdr[128];
        snprintf(hdr, sizeof(hdr), " OUTPUT [%s]  (Ctrl+J / :ho to hide) ", E.output_title);
        int w = ab_append_utf8_cols(ab, hdr, (int)strlen(hdr), total_cols);
        append_spaces(ab, total_cols - w);
        return;
    }

    ab_append_bg(ab, E.theme.sidebar_bg);
    ab_append_fg(ab, E.theme.fg);
    int out_rows = (E.cfg.output_height > 2 ? E.cfg.output_height : 8) - 1;
    int start = (E.output_len > out_rows) ? (E.output_len - out_rows) : 0;
    int idx = start + (row_idx - 1);
    if (idx < E.output_len) {
        ab_append_str(ab, " ");
        int w = 1 + ab_append_utf8_cols(ab, E.output_lines[idx],
                                        (int)strlen(E.output_lines[idx]), total_cols - 1);
        append_spaces(ab, total_cols - w);
    } else {
        append_spaces(ab, total_cols);
    }
}

void ui_render(void) {
    term_update_size();
    Buffer *b = &E.buffers[E.cur_buf];
    if (E.split_buf < 0 || E.split_buf >= E.num_buffers) E.split_buf = E.cur_buf;

    int top_rows = E.cfg.show_tabs ? 1 : 0;
    int out_h = (E.output_visible && E.screen_rows > 12) ? E.cfg.output_height : 0;
    int body_rows = E.screen_rows - top_rows - 2 - out_h; /* statusline + cmdline + output */
    if (body_rows < 1) body_rows = 1;

    int sb_w = 0;
    int border_w = 0;
    if (E.sidebar_visible && E.screen_cols > E.cfg.sidebar_width + 12) {
        sb_w = E.cfg.sidebar_width;
        border_w = 1;
    }
    int ed_total_cols = E.screen_cols - sb_w - border_w;
    if (ed_total_cols < 1) ed_total_cols = 1;

    int pane0_idx = (E.active_pane == 0) ? E.cur_buf : E.split_buf;
    int pane1_idx = (E.active_pane == 0) ? E.split_buf : E.cur_buf;
    Buffer *p0 = &E.buffers[pane0_idx];
    Buffer *p1 = &E.buffers[pane1_idx];

    int p0_cols = ed_total_cols, p1_cols = 0;
    int p0_rows = body_rows, p1_rows = 0;

    if (E.split_mode == SPLIT_VERT && ed_total_cols >= 24) {
        p0_cols = (ed_total_cols - 1) / 2;
        p1_cols = ed_total_cols - 1 - p0_cols;
        p1_rows = body_rows;
        scroll_editor(p0, p0_rows, p0_cols);
        scroll_editor(p1, p1_rows, p1_cols);
    } else if (E.split_mode == SPLIT_HORIZ && body_rows >= 6) {
        p0_rows = (body_rows - 1) / 2;
        p1_rows = body_rows - 1 - p0_rows;
        p1_cols = ed_total_cols;
        scroll_editor(p0, p0_rows, p0_cols);
        scroll_editor(p1, p1_rows, p1_cols);
    } else {
        scroll_editor(b, body_rows, ed_total_cols);
    }
    if (sb_w > 0) scroll_sidebar(body_rows);

    Abuf ab = {NULL, 0, 0};
    ab_append_str(&ab, "\x1b[?25l\x1b[H");

    if (E.cfg.show_tabs) {
        draw_tabline(&ab);
        ab_append_str(&ab, "\r\n");
    }

    for (int r = 0; r < body_rows; r++) {
        if (sb_w > 0) {
            draw_sidebar_cell(&ab, r, sb_w);
            ab_append_bg(&ab, E.theme.bg);
            ab_append_fg(&ab, E.theme.border_fg);
            ab_append_str(&ab, "│");
        }
        if (E.split_mode == SPLIT_VERT && p1_cols > 0) {
            draw_editor_row(&ab, p0, (E.active_pane == 0), r, p0_rows, p0_cols);
            ab_append_bg(&ab, E.theme.bg);
            ab_append_fg(&ab, E.theme.border_fg);
            ab_append_str(&ab, "│");
            draw_editor_row(&ab, p1, (E.active_pane == 1), r, p1_rows, p1_cols);
        } else if (E.split_mode == SPLIT_HORIZ && p1_rows > 0) {
            if (r < p0_rows) {
                draw_editor_row(&ab, p0, (E.active_pane == 0), r, p0_rows, ed_total_cols);
            } else if (r == p0_rows) {
                ab_append_bg(&ab, E.theme.bg);
                ab_append_fg(&ab, E.theme.border_fg);
                for (int c = 0; c < ed_total_cols; c++) ab_append_str(&ab, "─");
            } else {
                draw_editor_row(&ab, p1, (E.active_pane == 1), r - p0_rows - 1, p1_rows, ed_total_cols);
            }
        } else {
            draw_editor_row(&ab, b, 1, r, body_rows, ed_total_cols);
        }
        ab_append_str(&ab, "\r\n");
    }

    for (int r = 0; r < out_h; r++) {
        draw_output_row(&ab, r, E.screen_cols);
        ab_append_str(&ab, "\r\n");
    }

    draw_statusline(&ab, b);
    ab_append_str(&ab, "\r\n");
    draw_cmdline(&ab);

    /* Position hardware cursor */
    int cur_row = 1, cur_col = 1;
    if (E.mode == MODE_COMMAND) {
        cur_row = E.screen_rows;
        cur_col = 2 + utf8_str_width(E.cmd_buf, E.cmd_pos);
        ab_append_fmt(&ab, "\x1b[%d;%dH\x1b[?25h", cur_row, cur_col);
    } else if (E.mode == MODE_SEARCH) {
        cur_row = E.screen_rows;
        cur_col = 2 + utf8_str_width(E.search_buf, E.search_len);
        ab_append_fmt(&ab, "\x1b[%d;%dH\x1b[?25h", cur_row, cur_col);
    } else if (E.focus == FOCUS_EDITOR) {
        int gutter_w = E.cfg.line_numbers ? 6 : 1;
        int pane_col_off = sb_w + border_w;
        int pane_row_off = top_rows;
        if (E.split_mode == SPLIT_VERT && p1_cols > 0 && E.active_pane == 1) {
            pane_col_off += p0_cols + 1;
        } else if (E.split_mode == SPLIT_HORIZ && p1_rows > 0 && E.active_pane == 1) {
            pane_row_off += p0_rows + 1;
        }
        cur_row = pane_row_off + (b->cy - b->rowoff) + 1;
        cur_col = pane_col_off + gutter_w + (buf_cx_to_rx(b, b->cy, b->cx) - b->coloff) + 1;
        ab_append_fmt(&ab, "\x1b[%d;%dH\x1b[?25h", cur_row, cur_col);
    }

    ab_append_str(&ab, "\x1b[0m");
    ssize_t unused = write(STDOUT_FILENO, ab.b, (size_t)ab.len);
    (void)unused;
    ab_free(&ab);
}

void editor_set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
    E.status_msg_time = time(NULL);
}

void editor_prompt_input(const char *prompt, char *out, int out_sz) {
    int len = 0;
    out[0] = '\0';

    while (1) {
        editor_set_status("%s%s", prompt, out);
        ui_render();

        int ulen = 0;
        char ubuf[5];
        int key = term_read_key(&ulen, ubuf);
        if (key == KEY_NULL) continue;

        if (key == KEY_ESC) {
            out[0] = '\0';
            E.status_msg[0] = '\0';
            return;
        }
        if (key == KEY_ENTER) {
            E.status_msg[0] = '\0';
            return;
        }
        if (key == KEY_BACKSPACE || key == CTRL_KEY('h')) {
            if (len > 0) {
                len = utf8_prev_idx(out, len);
                out[len] = '\0';
            }
        } else if (ulen > 0 && (unsigned char)ubuf[0] >= 32 && key < 1000) {
            if (len + ulen < out_sz - 1) {
                memcpy(out + len, ubuf, (size_t)ulen);
                len += ulen;
                out[len] = '\0';
            }
        }
    }
}
