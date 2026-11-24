#include "state.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void tools_clear_output(const char *title) {
    E.output_len = 0;
    snprintf(E.output_title, sizeof(E.output_title), "%s",
             (title && title[0]) ? title : "Output");
}

void tools_append_output(const char *line) {
    if (!line) return;
    if (E.output_len >= MAX_OUTPUT_LINES) {
        memmove(E.output_lines[0], E.output_lines[1],
                (size_t)(MAX_OUTPUT_LINES - 1) * sizeof(E.output_lines[0]));
        E.output_len = MAX_OUTPUT_LINES - 1;
    }
    snprintf(E.output_lines[E.output_len++], sizeof(E.output_lines[0]), "%s", line);
}

void tools_toggle_output(void) {
    E.output_visible = !E.output_visible;
}

static int ends_with(const char *s, const char *suf) {
    size_t sl = strlen(s), sufl = strlen(suf);
    return sl >= sufl && strcmp(s + sl - sufl, suf) == 0;
}

void tools_run_current(void) {
    Buffer *b = &E.buffers[E.cur_buf];
    if (!b->filename || b->readonly) {
        editor_set_status("Open/save a runnable file first");
        return;
    }
    if (b->dirty) buf_save(b, NULL);

    char cmd[2048] = {0};
    const char *fn = b->filename;

    if (ends_with(fn, ".c")) {
        snprintf(cmd, sizeof(cmd),
                 "cc -Wall -Wextra -O2 '%s' -o /tmp/cti_run 2>&1 && /tmp/cti_run 2>&1", fn);
    } else if (ends_with(fn, ".cpp") || ends_with(fn, ".cc")) {
        snprintf(cmd, sizeof(cmd),
                 "c++ -Wall -Wextra -O2 '%s' -o /tmp/cti_run 2>&1 && /tmp/cti_run 2>&1", fn);
    } else if (ends_with(fn, ".py")) {
        snprintf(cmd, sizeof(cmd), "python3 '%s' 2>&1", fn);
    } else if (ends_with(fn, ".java")) {
        char dir[1024], cls[256];
        snprintf(dir, sizeof(dir), "%s", fn);
        char *sl = strrchr(dir, '/');
        if (sl) {
            *sl = '\0';
            snprintf(cls, sizeof(cls), "%s", sl + 1);
        } else {
            snprintf(dir, sizeof(dir), ".");
            snprintf(cls, sizeof(cls), "%s", fn);
        }
        char *dot = strrchr(cls, '.');
        if (dot) *dot = '\0';
        snprintf(cmd, sizeof(cmd),
                 "javac '%s' 2>&1 && java -cp '%s' '%s' 2>&1", fn, dir, cls);
    } else if (ends_with(fn, ".sh") || ends_with(fn, ".mksh")) {
        snprintf(cmd, sizeof(cmd), "sh '%s' 2>&1", fn);
    } else if (ends_with(fn, ".js")) {
        snprintf(cmd, sizeof(cmd), "node '%s' 2>&1", fn);
    } else if (ends_with(fn, "Makefile") || ends_with(fn, "makefile")) {
        snprintf(cmd, sizeof(cmd), "make -C '%s' 2>&1", E.root_dir);
    } else {
        editor_set_status("No runner configured for %s", fn);
        return;
    }

    tools_clear_output("Run");
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        tools_append_output("Failed to start process");
        E.output_visible = 1;
        return;
    }
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
        tools_append_output(line);
        count++;
    }
    int rc = pclose(fp);
    if (count == 0) tools_append_output("(no output)");
    char foot[128];
    snprintf(foot, sizeof(foot), "--- exited with status %d ---", rc >> 8);
    tools_append_output(foot);
    E.output_visible = 1;
    editor_set_status("Finished running %s (exit %d)", fn, rc >> 8);
}

void tools_spawn_shell(void) {
    const char *sh = getenv("SHELL");
    if (!sh || !sh[0]) sh = "/bin/sh";

    term_disable_raw();
    int rc = system(sh);
    (void)rc;
    term_enable_raw();
    term_update_size();
    tree_refresh();
    tools_refresh_git_branch();
    editor_set_status("Returned from shell");
}

static void grep_walk(const char *dir, const char *query, Abuf *out, int *matches) {
    if (*matches >= 300) return;
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *matches < 300) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (strcmp(ent->d_name, ".git") == 0 || strcmp(ent->d_name, "node_modules") == 0) continue;

        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            grep_walk(full, query, out, matches);
            continue;
        }
        if (st.st_size > 1024 * 1024) continue; /* skip >1MB files */

        FILE *f = fopen(full, "rb");
        if (!f) continue;

        char line[512];
        int lno = 0;
        while (fgets(line, sizeof(line), f)) {
            lno++;
            if (memchr(line, '\0', strlen(line)) == NULL) continue;
            if (strstr(line, query)) {
                size_t l = strlen(line);
                while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
                ab_append_fmt(out, "%s:%d: %s\n", full, lno, line);
                (*matches)++;
                if (*matches >= 300) break;
            }
        }
        fclose(f);
    }
    closedir(d);
}

void tools_grep(const char *query) {
    if (!query || !query[0]) {
        editor_set_status("Usage: :grep <pattern>");
        return;
    }

    Abuf out = {NULL, 0, 0};
    int matches = 0;
    ab_append_fmt(&out, "# Grep results for '%s' (press Enter or gf on a line to jump)\n\n", query);
    grep_walk(E.root_dir, query, &out, &matches);
    if (matches == 0) ab_append_str(&out, "No matches found.\n");

    buf_open_scratch("[Grep]", out.b ? out.b : "", 0);
    ab_free(&out);
    editor_set_status("Found %d match(es) for '%s'", matches, query);
}

void tools_jump_grep_line(Buffer *b) {
    if (!b || b->cy < 0 || b->cy >= b->num_lines) return;
    Line *ln = &b->lines[b->cy];
    if (!ln->chars || ln->size == 0) return;

    /* Parse "path:line: ..." */
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", ln->chars);
    char *c1 = strchr(tmp, ':');
    if (!c1) return;
    *c1 = '\0';
    int line_num = atoi(c1 + 1);
    if (line_num <= 0) return;

    struct stat st;
    if (stat(tmp, &st) == 0 && S_ISREG(st.st_mode)) {
        if (buf_open(tmp) >= 0) {
            Buffer *nb = &E.buffers[E.cur_buf];
            if (line_num > nb->num_lines) line_num = nb->num_lines;
            nb->cy = line_num - 1;
            nb->cx = 0;
            E.focus = FOCUS_EDITOR;
        }
    }
}

static void collect_files(const char *dir, const char *prefix,
                          char files[][512], int *count, int max_files) {
    if (*count >= max_files) return;
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *count < max_files) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (strcmp(ent->d_name, ".git") == 0) continue;
        if (!E.cfg.show_hidden && ent->d_name[0] == '.') continue;

        char full[2048], rel[512];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        if (prefix[0]) {
            snprintf(rel, sizeof(rel), "%s/%s", prefix, ent->d_name);
        } else {
            snprintf(rel, sizeof(rel), "%s", ent->d_name);
        }

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collect_files(full, rel, files, count, max_files);
        } else {
            snprintf(files[*count], 512, "%s", rel);
            (*count)++;
        }
    }
    closedir(d);
}

static int ci_contains(const char *hay, const char *needle) {
    if (!needle[0]) return 1;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nl) return 1;
    }
    return 0;
}

void tools_fuzzy_find(void) {
    char (*files)[512] = malloc(512 * 512);
    if (!files) return;
    int fcount = 0;
    collect_files(E.root_dir, "", files, &fcount, 512);

    char query[128] = {0};
    int qlen = 0;
    int sel = 0;

    while (1) {
        int matches[512];
        int mcount = 0;
        for (int i = 0; i < fcount; i++) {
            if (ci_contains(files[i], query)) matches[mcount++] = i;
        }
        if (sel >= mcount) sel = mcount > 0 ? mcount - 1 : 0;

        if (mcount > 0) {
            editor_set_status("Find [%d/%d]: %s  ->  %s",
                              sel + 1, mcount, query, files[matches[sel]]);
        } else {
            editor_set_status("Find [0/%d]: %s  (no match)", fcount, query);
        }
        ui_render();

        int ulen = 0;
        char ubuf[5];
        int key = term_read_key(&ulen, ubuf);
        if (key == KEY_NULL) continue;

        if (key == KEY_ESC) {
            E.status_msg[0] = '\0';
            break;
        }
        if (key == KEY_ENTER) {
            E.status_msg[0] = '\0';
            if (mcount > 0) {
                char full[2048];
                snprintf(full, sizeof(full), "%s/%s", E.root_dir, files[matches[sel]]);
                buf_open(full);
                E.focus = FOCUS_EDITOR;
            }
            break;
        }
        if (key == KEY_ARROW_DOWN || key == KEY_TAB || key == CTRL_KEY('n')) {
            if (mcount > 0) sel = (sel + 1) % mcount;
        } else if (key == KEY_ARROW_UP || key == KEY_SHIFT_TAB || key == CTRL_KEY('p')) {
            if (mcount > 0) sel = (sel - 1 + mcount) % mcount;
        } else if (key == KEY_BACKSPACE || key == CTRL_KEY('h')) {
            if (qlen > 0) {
                qlen = utf8_prev_idx(query, qlen);
                query[qlen] = '\0';
                sel = 0;
            }
        } else if (ulen > 0 && (unsigned char)ubuf[0] >= 32 && key < 1000) {
            if (qlen + ulen < (int)sizeof(query) - 1) {
                memcpy(query + qlen, ubuf, (size_t)ulen);
                qlen += ulen;
                query[qlen] = '\0';
                sel = 0;
            }
        }
    }
    free(files);
}

void tools_refresh_git_branch(void) {
    E.git_branch[0] = '\0';
    if (!E.cfg.git_branch) return;

    char cmd[1500];
    snprintf(cmd, sizeof(cmd),
             "git -C '%s' rev-parse --abbrev-ref HEAD 2>/dev/null", E.root_dir);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    if (fgets(E.git_branch, sizeof(E.git_branch), fp)) {
        size_t l = strlen(E.git_branch);
        while (l > 0 && (E.git_branch[l - 1] == '\n' || E.git_branch[l - 1] == '\r')) {
            E.git_branch[--l] = '\0';
        }
    }
    pclose(fp);
}

void tools_git_status(void) {
    tools_refresh_git_branch();
    char cmd[2200];
    snprintf(cmd, sizeof(cmd),
             "git -C '%s' status -sb 2>&1; echo ''; git -C '%s' log -n 8 --oneline 2>&1",
             E.root_dir, E.root_dir);
    FILE *fp = popen(cmd, "r");
    Abuf out = {NULL, 0, 0};
    if (fp) {
        char buf[512];
        while (fgets(buf, sizeof(buf), fp)) ab_append_str(&out, buf);
        pclose(fp);
    }
    buf_open_scratch("[Git Status]", out.b ? out.b : "Not a git repository\n", 0);
    ab_free(&out);
}

void tools_git_diff(void) {
    char cmd[1500];
    snprintf(cmd, sizeof(cmd), "git -C '%s' diff 2>&1", E.root_dir);
    FILE *fp = popen(cmd, "r");
    Abuf out = {NULL, 0, 0};
    if (fp) {
        char buf[512];
        while (fgets(buf, sizeof(buf), fp)) ab_append_str(&out, buf);
        pclose(fp);
    }
    if (out.len == 0) ab_append_str(&out, "Working tree clean (no unstaged changes).\n");
    buf_open_scratch("[Git Diff]", out.b, 0);
    ab_free(&out);
}

typedef struct {
    int total_files;
    long total_lines;
    long total_bytes;
    int c_files, py_files, java_files, sh_files, other_files;
    int todos;
} ProjectStats;

static void stats_walk(const char *dir, ProjectStats *st_out) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (strcmp(ent->d_name, ".git") == 0) continue;

        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            stats_walk(full, st_out);
            continue;
        }

        st_out->total_files++;
        st_out->total_bytes += (long)st.st_size;
        if (ends_with(ent->d_name, ".c") || ends_with(ent->d_name, ".h") ||
            ends_with(ent->d_name, ".cpp") || ends_with(ent->d_name, ".hpp"))
            st_out->c_files++;
        else if (ends_with(ent->d_name, ".py"))
            st_out->py_files++;
        else if (ends_with(ent->d_name, ".java"))
            st_out->java_files++;
        else if (ends_with(ent->d_name, ".sh") || strcmp(ent->d_name, "Makefile") == 0)
            st_out->sh_files++;
        else
            st_out->other_files++;

        if (st.st_size <= 512 * 1024) {
            FILE *f = fopen(full, "r");
            if (f) {
                char line[512];
                while (fgets(line, sizeof(line), f)) {
                    st_out->total_lines++;
                    if (strstr(line, "TODO") || strstr(line, "FIXME")) st_out->todos++;
                }
                fclose(f);
            }
        }
    }
    closedir(d);
}

void tools_project_stats(void) {
    ProjectStats ps;
    memset(&ps, 0, sizeof(ps));
    stats_walk(E.root_dir, &ps);

    Abuf out = {NULL, 0, 0};
    ab_append_str(&out, "==============================================================\n");
    ab_append_str(&out, "               catingiran Project Statistics\n");
    ab_append_str(&out, "==============================================================\n");
    ab_append_fmt(&out, "Root Directory : %s\n", E.root_dir);
    if (E.git_branch[0]) ab_append_fmt(&out, "Git Branch     : %s\n", E.git_branch);
    ab_append_fmt(&out, "Total Files    : %d\n", ps.total_files);
    ab_append_fmt(&out, "Total Lines    : %ld\n", ps.total_lines);
    ab_append_fmt(&out, "Total Size     : %.1f KB\n", ps.total_bytes / 1024.0);
    ab_append_fmt(&out, "TODO / FIXME   : %d\n\n", ps.todos);
    ab_append_str(&out, "Breakdown by language:\n");
    ab_append_fmt(&out, "  C / C++      : %d files\n", ps.c_files);
    ab_append_fmt(&out, "  Python       : %d files\n", ps.py_files);
    ab_append_fmt(&out, "  Java         : %d files\n", ps.java_files);
    ab_append_fmt(&out, "  Shell / Make : %d files\n", ps.sh_files);
    ab_append_fmt(&out, "  Other / Docs : %d files\n", ps.other_files);

    buf_open_scratch("[Stats]", out.b, 0);
    ab_free(&out);
}

void tools_toggle_comment(Buffer *b, int start_row, int end_row) {
    if (!b || start_row < 0 || end_row >= b->num_lines) return;
    const char *cmt = "# ";
    /* syntax_id 1=C, 3=Java, 4=JS */
    if (b->syntax_id == 1 || b->syntax_id == 3 || b->syntax_id == 4) {
        cmt = "// ";
    }
    int clen = (int)strlen(cmt);

    for (int r = start_row; r <= end_row; r++) {
        Line *ln = &b->lines[r];
        int i = 0;
        while (i < ln->size && (ln->chars[i] == ' ' || ln->chars[i] == '\t')) i++;
        if (i + clen <= ln->size && strncmp(ln->chars + i, cmt, (size_t)clen) == 0) {
            buf_raw_delete_at(b, r, i, clen);
        } else if (i + clen - 1 <= ln->size && strncmp(ln->chars + i, cmt, (size_t)(clen - 1)) == 0) {
            buf_raw_delete_at(b, r, i, clen - 1);
        } else {
            buf_raw_insert_at(b, r, i, cmt, clen);
        }
    }
}

void tools_duplicate_line(Buffer *b) {
    if (!b || b->cy < 0 || b->cy >= b->num_lines) return;
    Line *ln = &b->lines[b->cy];
    char *copy = malloc((size_t)ln->size + 1);
    if (!copy) return;
    memcpy(copy, ln->chars, (size_t)ln->size);
    copy[ln->size] = '\0';

    int orig_ai = E.cfg.auto_indent;
    E.cfg.auto_indent = 0;
    b->cx = ln->size;
    buf_insert_newline(b);
    E.cfg.auto_indent = orig_ai;
    buf_insert_bytes(b, copy, (int)strlen(copy));
    free(copy);
}

void tools_move_lines(Buffer *b, int start_row, int end_row, int dir) {
    if (!b || start_row < 0 || end_row >= b->num_lines) return;
    if (dir < 0 && start_row == 0) return;
    if (dir > 0 && end_row >= b->num_lines - 1) return;

    if (dir < 0) {
        Line moved = b->lines[start_row - 1];
        memmove(&b->lines[start_row - 1], &b->lines[start_row],
                (size_t)(end_row - start_row + 1) * sizeof(Line));
        b->lines[end_row] = moved;
        b->cy--;
        b->vis_sy--;
    } else {
        Line moved = b->lines[end_row + 1];
        memmove(&b->lines[start_row + 1], &b->lines[start_row],
                (size_t)(end_row - start_row + 1) * sizeof(Line));
        b->lines[start_row] = moved;
        b->cy++;
        b->vis_sy++;
    }
    b->dirty++;
    syntax_update_all(b);
}

void tools_toggle_bookmark(Buffer *b, int row) {
    if (!b || row < 0 || row >= b->num_lines) return;
    b->lines[row].bookmarked = !b->lines[row].bookmarked;
    editor_set_status("Bookmark %s on line %d",
                      b->lines[row].bookmarked ? "added" : "removed", row + 1);
}

void tools_next_bookmark(Buffer *b) {
    if (!b || b->num_lines == 0) return;
    for (int step = 1; step <= b->num_lines; step++) {
        int r = (b->cy + step) % b->num_lines;
        if (b->lines[r].bookmarked) {
            b->cy = r;
            b->cx = 0;
            editor_set_status("Jumped to bookmark at line %d", r + 1);
            return;
        }
    }
    editor_set_status("No bookmarks in buffer");
}
