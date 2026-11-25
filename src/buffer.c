#include "state.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

int utf8_prev_idx(const char *s, int pos) {
    if (pos <= 0) return 0;
    int p = pos - 1;
    while (p > 0 && (((unsigned char)s[p] & 0xC0) == 0x80)) {
        p--;
    }
    return p;
}

int utf8_next_idx(const char *s, int size, int pos) {
    if (pos >= size) return size;
    int step = utf8_char_len((unsigned char)s[pos]);
    if (pos + step > size) return size;
    return pos + step;
}

int utf8_str_width(const char *s, int bytes) {
    int w = 0;
    int i = 0;
    while (i < bytes) {
        unsigned char c = (unsigned char)s[i];
        int clen = utf8_char_len(c);
        if (i + clen > bytes) clen = bytes - i;
        if (c == '\t') {
            int ts = E.cfg.tab_size > 0 ? E.cfg.tab_size : 4;
            w += ts - (w % ts);
        } else {
            w += (clen == 4) ? 2 : 1;
        }
        i += clen;
    }
    return w;
}

int buf_cx_to_rx(Buffer *b, int cy, int cx) {
    if (cy < 0 || cy >= b->num_lines) return 0;
    Line *ln = &b->lines[cy];
    if (cx > ln->size) cx = ln->size;
    return utf8_str_width(ln->chars, cx);
}

static void line_ensure_cap(Line *ln, int needed) {
    if (needed <= ln->cap) return;
    int ncap = ln->cap ? ln->cap * 2 : 64;
    while (ncap < needed) ncap *= 2;
    char *nc = realloc(ln->chars, (size_t)ncap);
    if (!nc) return;
    ln->chars = nc;
    ln->cap = ncap;
}

static void buf_ensure_lines(Buffer *b, int needed) {
    if (needed <= b->cap_lines) return;
    int ncap = b->cap_lines ? b->cap_lines * 2 : 32;
    while (ncap < needed) ncap *= 2;
    Line *nl = realloc(b->lines, (size_t)ncap * sizeof(Line));
    if (!nl) return;
    b->lines = nl;
    b->cap_lines = ncap;
}

static int buf_find_idx(Buffer *b) {
    for (int i = 0; i < E.num_buffers; i++) {
        if (&E.buffers[i] == b) return i;
    }
    return -1;
}

static void redo_clear(Buffer *b) {
    for (int i = 0; i < b->redo_len; i++) {
        free(b->redo[i].text);
    }
    b->redo_len = 0;
}

static void redo_push_act(Buffer *b, UndoAction u) {
    if (b->redo_len >= b->redo_cap) {
        int ncap = b->redo_cap ? b->redo_cap * 2 : 128;
        UndoAction *na = realloc(b->redo, (size_t)ncap * sizeof(UndoAction));
        if (!na) {
            free(u.text);
            return;
        }
        b->redo = na;
        b->redo_cap = ncap;
    }
    b->redo[b->redo_len++] = u;
}

static void undo_push_act(Buffer *b, UndoAction u) {
    int max_lvl = E.cfg.undo_levels > 0 ? E.cfg.undo_levels : 500;
    while (b->undo_len >= max_lvl) {
        free(b->undo[0].text);
        memmove(&b->undo[0], &b->undo[1], (size_t)(b->undo_len - 1) * sizeof(UndoAction));
        b->undo_len--;
    }
    if (b->undo_len >= b->undo_cap) {
        int ncap = b->undo_cap ? b->undo_cap * 2 : 128;
        UndoAction *na = realloc(b->undo, (size_t)ncap * sizeof(UndoAction));
        if (!na) {
            free(u.text);
            return;
        }
        b->undo = na;
        b->undo_cap = ncap;
    }
    b->undo[b->undo_len++] = u;
}

static void undo_push(Buffer *b, UndoKind kind, int row, int col, const char *text, int len) {
    if (!b->undo_recording) return;
    redo_clear(b);
    UndoAction u;
    u.kind = kind;
    u.row = row;
    u.col = col;
    u.len = len;
    if (text && len > 0) {
        u.text = malloc((size_t)len + 1);
        if (u.text) {
            memcpy(u.text, text, (size_t)len);
            u.text[len] = '\0';
        }
    } else {
        u.text = NULL;
    }
    undo_push_act(b, u);
}

static void buf_raw_insert_line(Buffer *b, int at, const char *s, int len) {
    if (at < 0 || at > b->num_lines) return;
    buf_ensure_lines(b, b->num_lines + 1);
    memmove(&b->lines[at + 1], &b->lines[at], (size_t)(b->num_lines - at) * sizeof(Line));
    Line *ln = &b->lines[at];
    memset(ln, 0, sizeof(Line));
    line_ensure_cap(ln, len + 1);
    if (len > 0 && s) memcpy(ln->chars, s, (size_t)len);
    ln->chars[len] = '\0';
    ln->size = len;
    b->num_lines++;
    syntax_update_line(b, at);
}

static void buf_raw_free_line(Line *ln) {
    free(ln->chars);
    free(ln->hl);
    memset(ln, 0, sizeof(Line));
}

static void buf_raw_del_line(Buffer *b, int at) {
    if (at < 0 || at >= b->num_lines) return;
    buf_raw_free_line(&b->lines[at]);
    memmove(&b->lines[at], &b->lines[at + 1], (size_t)(b->num_lines - at - 1) * sizeof(Line));
    b->num_lines--;
}

static void line_raw_insert(Buffer *b, int row, int col, const char *s, int len) {
    Line *ln = &b->lines[row];
    if (col < 0) col = 0;
    if (col > ln->size) col = ln->size;
    line_ensure_cap(ln, ln->size + len + 1);
    memmove(ln->chars + col + len, ln->chars + col, (size_t)(ln->size - col + 1));
    memcpy(ln->chars + col, s, (size_t)len);
    ln->size += len;
    syntax_update_line(b, row);
}

static void line_raw_delete(Buffer *b, int row, int col, int len) {
    Line *ln = &b->lines[row];
    if (col < 0 || col >= ln->size || len <= 0) return;
    if (col + len > ln->size) len = ln->size - col;
    memmove(ln->chars + col, ln->chars + col + len, (size_t)(ln->size - col - len + 1));
    ln->size -= len;
    syntax_update_line(b, row);
}

void buf_raw_insert_at(Buffer *b, int row, int col, const char *s, int len) {
    line_raw_insert(b, row, col, s, len);
    b->dirty++;
    lsp_on_buf_change(buf_find_idx(b));
}

void buf_raw_delete_at(Buffer *b, int row, int col, int len) {
    line_raw_delete(b, row, col, len);
    b->dirty++;
    lsp_on_buf_change(buf_find_idx(b));
}

void buf_init_empty(Buffer *b) {
    memset(b, 0, sizeof(Buffer));
    b->undo_recording = 1;
    buf_raw_insert_line(b, 0, "", 0);
}

void buf_free(Buffer *b) {
    free(b->filename);
    for (int i = 0; i < b->num_lines; i++) {
        buf_raw_free_line(&b->lines[i]);
    }
    free(b->lines);
    for (int i = 0; i < b->undo_len; i++) {
        free(b->undo[i].text);
    }
    free(b->undo);
    for (int i = 0; i < b->redo_len; i++) {
        free(b->redo[i].text);
    }
    free(b->redo);
    memset(b, 0, sizeof(Buffer));
}

int buf_open(const char *filename) {
    /* Check if already open */
    if (filename && filename[0]) {
        for (int i = 0; i < E.num_buffers; i++) {
            if (E.buffers[i].filename && strcmp(E.buffers[i].filename, filename) == 0) {
                E.cur_buf = i;
                return i;
            }
        }
    }

    int slot = -1;
    /* Reuse unmodified empty untitled buffer if it's the only one */
    if (E.num_buffers == 1 && !E.buffers[0].filename && !E.buffers[0].dirty &&
        E.buffers[0].num_lines == 1 && E.buffers[0].lines[0].size == 0) {
        buf_free(&E.buffers[0]);
        slot = 0;
    } else if (E.num_buffers < MAX_BUFFERS) {
        slot = E.num_buffers++;
    } else {
        editor_set_status("Max tabs (%d) reached!", MAX_BUFFERS);
        return -1;
    }

    Buffer *b = &E.buffers[slot];
    memset(b, 0, sizeof(Buffer));
    b->undo_recording = 1;

    if (filename && filename[0]) {
        b->filename = strdup(filename);
        b->syntax_id = syntax_detect(filename);

        FILE *fp = fopen(filename, "rb");
        if (fp) {
            char *line = NULL;
            size_t cap = 0;
            ssize_t linelen;
            while ((linelen = getline(&line, &cap, fp)) != -1) {
                while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
                    linelen--;
                }
                buf_raw_insert_line(b, b->num_lines, line, (int)linelen);
            }
            free(line);
            fclose(fp);
        }
    }

    if (b->num_lines == 0) {
        buf_raw_insert_line(b, 0, "", 0);
    }

    syntax_update_all(b);
    b->dirty = 0;
    E.cur_buf = slot;
    if (b->filename) lsp_on_buf_open(slot);
    return slot;
}

int buf_open_scratch(const char *title, const char *content, int syntax_id) {
    int slot = -1;
    for (int i = 0; i < E.num_buffers; i++) {
        if (E.buffers[i].filename && strcmp(E.buffers[i].filename, title) == 0) {
            buf_free(&E.buffers[i]);
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        if (E.num_buffers < MAX_BUFFERS) {
            slot = E.num_buffers++;
        } else {
            slot = MAX_BUFFERS - 1;
            buf_free(&E.buffers[slot]);
        }
    }

    Buffer *b = &E.buffers[slot];
    memset(b, 0, sizeof(Buffer));
    b->undo_recording = 0;
    b->readonly = 1;
    b->filename = strdup(title);
    b->syntax_id = syntax_id;

    const char *p = content ? content : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        buf_raw_insert_line(b, b->num_lines, p, len);
        if (!nl) break;
        p = nl + 1;
    }
    if (b->num_lines == 0) {
        buf_raw_insert_line(b, 0, "", 0);
    }
    syntax_update_all(b);
    b->dirty = 0;
    b->undo_recording = 1;
    E.cur_buf = slot;
    E.focus = FOCUS_EDITOR;
    return slot;
}

int buf_save(Buffer *b, const char *new_name) {
    int had_name = (b->filename != NULL);
    if (new_name && new_name[0]) {
        free(b->filename);
        b->filename = strdup(new_name);
        b->syntax_id = syntax_detect(b->filename);
        syntax_update_all(b);
    }
    if (!b->filename) {
        editor_set_status("No file name (use :w filename)");
        return 0;
    }

    if (E.cfg.trim_on_save) {
        for (int i = 0; i < b->num_lines; i++) {
            Line *ln = &b->lines[i];
            int trim = 0;
            while (ln->size - trim > 0 &&
                   (ln->chars[ln->size - trim - 1] == ' ' ||
                    ln->chars[ln->size - trim - 1] == '\t')) {
                trim++;
            }
            if (trim > 0) {
                int col = ln->size - trim;
                undo_push(b, UNDO_DEL_STR, i, col, ln->chars + col, trim);
                line_raw_delete(b, i, col, trim);
                if (b->cy == i && b->cx > ln->size) b->cx = ln->size;
            }
        }
    }

    FILE *fp = fopen(b->filename, "wb");
    if (!fp) {
        editor_set_status("Error saving %s: %s", b->filename, strerror(errno));
        return 0;
    }

    size_t total_bytes = 0;
    for (int i = 0; i < b->num_lines; i++) {
        if (b->lines[i].size > 0) {
            fwrite(b->lines[i].chars, 1, (size_t)b->lines[i].size, fp);
            total_bytes += (size_t)b->lines[i].size;
        }
        fputc('\n', fp);
        total_bytes++;
    }
    if (fclose(fp) != 0) {
        editor_set_status("Error closing %s: %s", b->filename, strerror(errno));
        return 0;
    }

    b->dirty = 0;
    editor_set_status("\"%s\" %dL, %zuB written", b->filename, b->num_lines, total_bytes);
    tree_refresh();
    int idx = buf_find_idx(b);
    if (!had_name || !b->lsp_opened) {
        lsp_on_buf_open(idx);
    } else {
        lsp_on_buf_save(idx);
    }
    if (E.cfg.auto_lint) tools_lint_buffer(b);
    return 1;
}

void buf_close(int idx, int force) {
    if (idx < 0 || idx >= E.num_buffers) return;
    if (E.buffers[idx].dirty && !force) {
        editor_set_status("Unsaved changes! Use :bd! or :q! to discard");
        return;
    }

    buf_free(&E.buffers[idx]);
    if (E.num_buffers == 1) {
        buf_init_empty(&E.buffers[0]);
        E.cur_buf = 0;
        return;
    }

    memmove(&E.buffers[idx], &E.buffers[idx + 1],
            (size_t)(E.num_buffers - idx - 1) * sizeof(Buffer));
    E.num_buffers--;
    if (E.cur_buf >= E.num_buffers) E.cur_buf = E.num_buffers - 1;
}

void buf_insert_bytes(Buffer *b, const char *s, int len) {
    if (b->cy < 0 || b->cy >= b->num_lines) return;
    undo_push(b, UNDO_INS_STR, b->cy, b->cx, s, len);
    line_raw_insert(b, b->cy, b->cx, s, len);
    b->cx += len;
    b->dirty++;
    if (b->syntax_id == SYN_NONE && !b->syntax_manual && !b->filename) {
        int det = syntax_detect_from_content(b);
        if (det != SYN_NONE) {
            b->syntax_id = det;
            syntax_update_all(b);
        }
    }
    lsp_on_buf_change(buf_find_idx(b));
}

void buf_insert_newline(Buffer *b) {
    if (b->cy < 0 || b->cy >= b->num_lines) return;
    Line *ln = &b->lines[b->cy];

    /* Compute auto-indent prefix from current line */
    char indent[128];
    int ind_len = 0;
    if (E.cfg.auto_indent) {
        while (ind_len < ln->size && ind_len < b->cx &&
               ind_len < (int)sizeof(indent) - 1 &&
               (ln->chars[ind_len] == ' ' || ln->chars[ind_len] == '\t')) {
            indent[ind_len] = ln->chars[ind_len];
            ind_len++;
        }
    }
    indent[ind_len] = '\0';

    int tail_len = ln->size - b->cx;
    char *new_line = malloc((size_t)ind_len + (size_t)tail_len + 1);
    if (!new_line) return;
    if (ind_len > 0) memcpy(new_line, indent, (size_t)ind_len);
    if (tail_len > 0) memcpy(new_line + ind_len, ln->chars + b->cx, (size_t)tail_len);
    new_line[ind_len + tail_len] = '\0';

    undo_push(b, UNDO_SPLIT_LINE, b->cy, b->cx, indent, ind_len);

    /* Truncate current line */
    ln->size = b->cx;
    ln->chars[b->cx] = '\0';
    syntax_update_line(b, b->cy);

    buf_raw_insert_line(b, b->cy + 1, new_line, ind_len + tail_len);
    free(new_line);

    b->cy++;
    b->cx = ind_len;
    b->dirty++;
    lsp_on_buf_change(buf_find_idx(b));
}

void buf_backspace(Buffer *b) {
    if (b->cy == 0 && b->cx == 0) return;
    if (b->cx > 0) {
        Line *ln = &b->lines[b->cy];
        int prev = utf8_prev_idx(ln->chars, b->cx);
        int del_len = b->cx - prev;
        undo_push(b, UNDO_DEL_STR, b->cy, prev, ln->chars + prev, del_len);
        line_raw_delete(b, b->cy, prev, del_len);
        b->cx = prev;
        b->dirty++;
    } else {
        int prev_row = b->cy - 1;
        int prev_len = b->lines[prev_row].size;
        Line *cur = &b->lines[b->cy];
        undo_push(b, UNDO_JOIN_LINE, prev_row, prev_len, NULL, 0);
        if (cur->size > 0) {
            line_raw_insert(b, prev_row, prev_len, cur->chars, cur->size);
        }
        buf_raw_del_line(b, b->cy);
        b->cy = prev_row;
        b->cx = prev_len;
        b->dirty++;
    }
    lsp_on_buf_change(buf_find_idx(b));
}

void buf_delete_at_cursor(Buffer *b) {
    Line *ln = &b->lines[b->cy];
    if (b->cx < ln->size) {
        int next = utf8_next_idx(ln->chars, ln->size, b->cx);
        int del_len = next - b->cx;
        undo_push(b, UNDO_DEL_STR, b->cy, b->cx, ln->chars + b->cx, del_len);
        line_raw_delete(b, b->cy, b->cx, del_len);
        b->dirty++;
    } else if (b->cy + 1 < b->num_lines) {
        Line *next_ln = &b->lines[b->cy + 1];
        undo_push(b, UNDO_JOIN_LINE, b->cy, ln->size, NULL, 0);
        if (next_ln->size > 0) {
            line_raw_insert(b, b->cy, ln->size, next_ln->chars, next_ln->size);
        }
        buf_raw_del_line(b, b->cy + 1);
        b->dirty++;
    }
    lsp_on_buf_change(buf_find_idx(b));
}

void buf_delete_line(Buffer *b, int row) {
    if (row < 0 || row >= b->num_lines) return;
    Line *ln = &b->lines[row];
    free(E.clipboard);
    E.clipboard = malloc((size_t)ln->size + 2);
    if (E.clipboard) {
        memcpy(E.clipboard, ln->chars, (size_t)ln->size);
        E.clipboard[ln->size] = '\n';
        E.clipboard[ln->size + 1] = '\0';
    }

    if (b->num_lines == 1) {
        if (ln->size > 0) {
            undo_push(b, UNDO_DEL_STR, 0, 0, ln->chars, ln->size);
            line_raw_delete(b, 0, 0, ln->size);
            b->cx = 0;
            b->dirty++;
            lsp_on_buf_change(buf_find_idx(b));
        }
        return;
    }

    undo_push(b, UNDO_DEL_LINE, row, 0, ln->chars, ln->size);
    buf_raw_del_line(b, row);
    if (b->cy >= b->num_lines) b->cy = b->num_lines - 1;
    if (b->cx > b->lines[b->cy].size) b->cx = b->lines[b->cy].size;
    b->dirty++;
    lsp_on_buf_change(buf_find_idx(b));
}

void buf_delete_range(Buffer *b, int sr, int sc, int er, int ec, int copy_to_clip) {
    if (b->num_lines <= 0) return;
    if (sr > er || (sr == er && sc > ec)) {
        int tr = sr, tc = sc;
        sr = er; sc = ec;
        er = tr; ec = tc;
    }
    if (sr < 0) sr = 0;
    if (er >= b->num_lines) er = b->num_lines - 1;
    if (sc < 0) sc = 0;
    if (sc > b->lines[sr].size) sc = b->lines[sr].size;
    if (ec < 0) ec = 0;
    if (ec > b->lines[er].size) ec = b->lines[er].size;

    if (copy_to_clip) {
        Abuf clip = {0};
        if (sr == er) {
            if (ec > sc) ab_append(&clip, b->lines[sr].chars + sc, ec - sc);
        } else {
            ab_append(&clip, b->lines[sr].chars + sc, b->lines[sr].size - sc);
            ab_append_str(&clip, "\n");
            for (int r = sr + 1; r < er; r++) {
                ab_append(&clip, b->lines[r].chars, b->lines[r].size);
                ab_append_str(&clip, "\n");
            }
            ab_append(&clip, b->lines[er].chars, ec);
        }
        ab_append(&clip, "\0", 1);
        free(E.clipboard);
        E.clipboard = clip.b;
    }

    if (sr == er) {
        int len = ec - sc;
        if (len > 0) {
            undo_push(b, UNDO_DEL_STR, sr, sc, b->lines[sr].chars + sc, len);
            line_raw_delete(b, sr, sc, len);
            b->dirty++;
            lsp_on_buf_change(buf_find_idx(b));
        }
        b->cy = sr;
        b->cx = sc;
        return;
    }

    int tail_len = b->lines[er].size - ec;
    char *tail = NULL;
    if (tail_len > 0) {
        tail = malloc((size_t)tail_len + 1);
        if (tail) {
            memcpy(tail, b->lines[er].chars + ec, (size_t)tail_len);
            tail[tail_len] = '\0';
        }
    }

    for (int r = er; r >= sr + 1; r--) {
        undo_push(b, UNDO_DEL_LINE, r, 0, b->lines[r].chars, b->lines[r].size);
        buf_raw_del_line(b, r);
    }

    int head_del = b->lines[sr].size - sc;
    if (head_del > 0) {
        undo_push(b, UNDO_DEL_STR, sr, sc, b->lines[sr].chars + sc, head_del);
        line_raw_delete(b, sr, sc, head_del);
    }

    if (tail && tail_len > 0) {
        undo_push(b, UNDO_INS_STR, sr, sc, tail, tail_len);
        line_raw_insert(b, sr, sc, tail, tail_len);
    }
    free(tail);

    b->cy = sr;
    b->cx = sc;
    b->dirty++;
    lsp_on_buf_change(buf_find_idx(b));
}

void buf_indent_line(Buffer *b, int row, int delta) {
    if (row < 0 || row >= b->num_lines) return;
    int ts = E.cfg.tab_size > 0 ? E.cfg.tab_size : 4;
    Line *ln = &b->lines[row];

    if (delta > 0) {
        char spaces[17];
        if (ts > 16) ts = 16;
        memset(spaces, ' ', (size_t)ts);
        undo_push(b, UNDO_INS_STR, row, 0, spaces, ts);
        line_raw_insert(b, row, 0, spaces, ts);
        if (b->cy == row) b->cx += ts;
        b->dirty++;
        lsp_on_buf_change(buf_find_idx(b));
    } else if (delta < 0) {
        int remove = 0;
        if (ln->size > 0 && ln->chars[0] == '\t') {
            remove = 1;
        } else {
            while (remove < ts && remove < ln->size && ln->chars[remove] == ' ') {
                remove++;
            }
        }
        if (remove > 0) {
            undo_push(b, UNDO_DEL_STR, row, 0, ln->chars, remove);
            line_raw_delete(b, row, 0, remove);
            if (b->cy == row) {
                b->cx = (b->cx > remove) ? (b->cx - remove) : 0;
            }
            b->dirty++;
            lsp_on_buf_change(buf_find_idx(b));
        }
    }
}

void buf_undo(Buffer *b) {
    if (b->undo_len <= 0) {
        editor_set_status("Already at oldest change");
        return;
    }

    UndoAction u = b->undo[--b->undo_len];
    b->undo_recording = 0;

    switch (u.kind) {
        case UNDO_INS_STR:
            line_raw_delete(b, u.row, u.col, u.len);
            b->cy = u.row;
            b->cx = u.col;
            break;
        case UNDO_DEL_STR:
            line_raw_insert(b, u.row, u.col, u.text ? u.text : "", u.len);
            b->cy = u.row;
            b->cx = u.col + u.len;
            break;
        case UNDO_SPLIT_LINE: {
            if (u.row + 1 < b->num_lines) {
                Line *next_ln = &b->lines[u.row + 1];
                int skip = u.len;
                if (skip > next_ln->size) skip = next_ln->size;
                if (next_ln->size > skip) {
                    line_raw_insert(b, u.row, u.col, next_ln->chars + skip, next_ln->size - skip);
                }
                buf_raw_del_line(b, u.row + 1);
                b->cy = u.row;
                b->cx = u.col;
            }
            break;
        }
        case UNDO_JOIN_LINE: {
            Line *ln = &b->lines[u.row];
            int tail = ln->size - u.col;
            if (tail < 0) tail = 0;
            buf_raw_insert_line(b, u.row + 1, ln->chars + u.col, tail);
            ln->size = u.col;
            ln->chars[u.col] = '\0';
            syntax_update_line(b, u.row);
            b->cy = u.row + 1;
            b->cx = 0;
            break;
        }
        case UNDO_DEL_LINE:
            buf_raw_insert_line(b, u.row, u.text ? u.text : "", u.len);
            b->cy = u.row;
            b->cx = 0;
            break;
        case UNDO_INS_LINE:
            if (u.row >= 0 && u.row < b->num_lines && !u.text) {
                Line *ln = &b->lines[u.row];
                u.len = ln->size;
                u.text = malloc((size_t)ln->size + 1);
                if (u.text) {
                    if (ln->size > 0) memcpy(u.text, ln->chars, (size_t)ln->size);
                    u.text[ln->size] = '\0';
                }
            }
            buf_raw_del_line(b, u.row);
            if (b->num_lines == 0) buf_raw_insert_line(b, 0, "", 0);
            b->cy = u.row < b->num_lines ? u.row : b->num_lines - 1;
            b->cx = 0;
            break;
    }

    redo_push_act(b, u);
    b->undo_recording = 1;
    if (b->dirty > 0) b->dirty--;
    lsp_on_buf_change(buf_find_idx(b));
    editor_set_status("Undo");
}

void buf_redo(Buffer *b) {
    if (b->redo_len <= 0) {
        editor_set_status("Already at newest change");
        return;
    }

    UndoAction u = b->redo[--b->redo_len];
    b->undo_recording = 0;

    switch (u.kind) {
        case UNDO_INS_STR:
            line_raw_insert(b, u.row, u.col, u.text ? u.text : "", u.len);
            b->cy = u.row;
            b->cx = u.col + u.len;
            break;
        case UNDO_DEL_STR:
            line_raw_delete(b, u.row, u.col, u.len);
            b->cy = u.row;
            b->cx = u.col;
            break;
        case UNDO_SPLIT_LINE: {
            Line *ln = &b->lines[u.row];
            int tail = ln->size - u.col;
            if (tail < 0) tail = 0;
            int ind_len = u.len;
            char *nl = malloc((size_t)ind_len + (size_t)tail + 1);
            if (nl) {
                if (ind_len > 0 && u.text) memcpy(nl, u.text, (size_t)ind_len);
                if (tail > 0) memcpy(nl + ind_len, ln->chars + u.col, (size_t)tail);
                nl[ind_len + tail] = '\0';
                ln->size = u.col;
                ln->chars[u.col] = '\0';
                syntax_update_line(b, u.row);
                buf_raw_insert_line(b, u.row + 1, nl, ind_len + tail);
                free(nl);
            }
            b->cy = u.row + 1;
            b->cx = ind_len;
            break;
        }
        case UNDO_JOIN_LINE:
            if (u.row + 1 < b->num_lines) {
                Line *next_ln = &b->lines[u.row + 1];
                if (next_ln->size > 0) {
                    line_raw_insert(b, u.row, u.col, next_ln->chars, next_ln->size);
                }
                buf_raw_del_line(b, u.row + 1);
                b->cy = u.row;
                b->cx = u.col;
            }
            break;
        case UNDO_DEL_LINE:
            buf_raw_del_line(b, u.row);
            if (b->num_lines == 0) buf_raw_insert_line(b, 0, "", 0);
            b->cy = u.row < b->num_lines ? u.row : b->num_lines - 1;
            b->cx = 0;
            break;
        case UNDO_INS_LINE:
            buf_raw_insert_line(b, u.row, u.text ? u.text : "", u.len);
            b->cy = u.row;
            b->cx = 0;
            break;
    }

    undo_push_act(b, u);
    b->undo_recording = 1;
    b->dirty++;
    lsp_on_buf_change(buf_find_idx(b));
    editor_set_status("Redo");
}

static int use_case_insensitive(const char *q) {
    if (!E.cfg.ignore_case) return 0;
    if (E.cfg.smart_case && q) {
        for (int i = 0; q[i]; i++) {
            if (isupper((unsigned char)q[i])) return 0;
        }
    }
    return 1;
}

static int match_at(const char *hay, const char *needle, int nlen, int ci) {
    if (!ci) return strncmp(hay, needle, (size_t)nlen) == 0;
    for (int i = 0; i < nlen; i++) {
        if (tolower((unsigned char)hay[i]) != tolower((unsigned char)needle[i])) return 0;
    }
    return 1;
}

int buf_replace_all(Buffer *b, const char *old_s, const char *new_s, int cur_line_only) {
    if (!old_s || !old_s[0] || b->num_lines <= 0) return 0;
    int olen = (int)strlen(old_s);
    int nlen = new_s ? (int)strlen(new_s) : 0;
    int ci = use_case_insensitive(old_s);
    int sr = cur_line_only ? b->cy : 0;
    int er = cur_line_only ? b->cy : (b->num_lines - 1);
    int count = 0;

    for (int r = sr; r <= er; r++) {
        int c = 0;
        while (c <= b->lines[r].size - olen) {
            if (match_at(b->lines[r].chars + c, old_s, olen, ci)) {
                undo_push(b, UNDO_DEL_STR, r, c, b->lines[r].chars + c, olen);
                line_raw_delete(b, r, c, olen);
                if (nlen > 0) {
                    undo_push(b, UNDO_INS_STR, r, c, new_s, nlen);
                    line_raw_insert(b, r, c, new_s, nlen);
                }
                c += nlen;
                count++;
                b->dirty++;
            } else {
                c++;
            }
        }
    }
    if (b->cx > b->lines[b->cy].size) b->cx = b->lines[b->cy].size;
    if (count > 0) lsp_on_buf_change(buf_find_idx(b));
    return count;
}

void buf_find(Buffer *b, const char *query, int dir) {
    if (!query || !query[0] || b->num_lines == 0) return;

    int qlen = (int)strlen(query);
    int ci = use_case_insensitive(query);
    int start_row = b->cy;
    int start_col = b->cx + (dir > 0 ? 1 : -1);

    for (int step = 0; step <= b->num_lines; step++) {
        int r = (start_row + step * dir + b->num_lines * 2) % b->num_lines;
        Line *ln = &b->lines[r];
        if (ln->size < qlen) continue;

        if (dir > 0) {
            int c0 = (step == 0 && start_col > 0) ? start_col : 0;
            for (int c = c0; c <= ln->size - qlen; c++) {
                if (match_at(ln->chars + c, query, qlen, ci)) {
                    b->cy = r;
                    b->cx = c;
                    E.search_match_row = r;
                    E.search_match_col = c;
                    return;
                }
            }
        } else {
            int c0 = (step == 0 && start_col >= 0 && start_col <= ln->size - qlen)
                     ? start_col : (ln->size - qlen);
            for (int c = c0; c >= 0; c--) {
                if (match_at(ln->chars + c, query, qlen, ci)) {
                    b->cy = r;
                    b->cx = c;
                    E.search_match_row = r;
                    E.search_match_col = c;
                    return;
                }
            }
        }
    }
    editor_set_status("Pattern not found: %s", query);
}
