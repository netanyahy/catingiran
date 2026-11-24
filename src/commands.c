#include "state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_dirty_buffers(void) {
    for (int i = 0; i < E.num_buffers; i++) {
        if (E.buffers[i].dirty) return 1;
    }
    return 0;
}

static void open_help_buffer(void) {
    static const char *help_lines[] = {
        "==============================================================",
        "               catingiran (cti) v" CTI_VERSION " - Quick Help",
        "==============================================================",
        "",
        "COMMAND LINE (:)",
        "  :w [file]        Save current file (or save as)",
        "  :q / :q!         Quit (force quit without saving)",
        "  :wq / :x         Save and quit",
        "  :e <file>        Open file in new tab",
        "  :new             Open empty untitled tab",
        "  :bd / :c         Close current tab",
        "  :t1 .. :t9       Jump to tab 1..9 (also :e1..:e9 in RU)",
        "  :ot / :ht        Open / Hide file explorer sidebar (also :ot/:nt in RU)",
        "  :tree            Toggle file explorer sidebar",
        "  :theme <name>    Switch theme (" "charcoal, obsidian, catppuccin, tokyonight, nord, light" ")",
        "  :themes          List available themes",
        "  :set number      Show line numbers (:set nonumber to hide)",
        "  :set icons       Enable Nerd Font icons (:set noicons to disable)",
        "  :reload          Reload catingiran.conf",
        "",
        "KEYBINDINGS (NORMAL MODE)",
        "  h / j / k / l    Move cursor (also Russian layout if enabled)",
        "  w / b / 0 / $    Word forward/back, line start/end",
        "  gg / G           Top / bottom of file",
        "  i / a / o / O    Insert mode",
        "  v                Visual mode (y=yank, d=delete, >/< =indent)",
        "  dd / yy / p / u  Delete line / Yank line / Paste / Undo",
        "  /pattern         Search forward (n=next, N=prev)",
        "  Ctrl+B / F2      Toggle file explorer sidebar",
        "  Ctrl+H / Ctrl+L  Focus sidebar / Focus code editor",
        "  Tab / Shift+Tab  Next / Previous tab",
        "  Space + 1..9     Jump to tab 1..9",
        "  Space + e        Toggle sidebar",
        "  Space + w / q    Save / Quit",
        "",
        "FILE EXPLORER SIDEBAR (when focused)",
        "  j / k / Enter    Navigate / Open file or expand folder",
        "  h / Left         Collapse folder or jump to parent",
        "  a / d / r        Create file (dir/ for folder) / Delete / Rename",
        "  H / R            Toggle hidden files / Refresh tree",
        "",
        "CONFIG FILE",
        "  ~/.config/catingiran/catingiran.conf",
        "=============================================================="
    };

    if (E.num_buffers >= MAX_BUFFERS) {
        editor_set_status("Close a tab first to view :help");
        return;
    }

    int slot = E.num_buffers++;
    Buffer *b = &E.buffers[slot];
    buf_init_empty(b);
    b->filename = strdup("[Help]");
    b->readonly = 1;
    b->undo_recording = 0;

    /* Replace initial empty line with help content */
    b->lines[0].size = 0;
    b->lines[0].chars[0] = '\0';
    int n = (int)(sizeof(help_lines) / sizeof(help_lines[0]));
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            b->cy = i - 1;
            b->cx = b->lines[i - 1].size;
            buf_insert_newline(b);
        }
        buf_insert_bytes(b, help_lines[i], (int)strlen(help_lines[i]));
    }
    b->cy = 0;
    b->cx = 0;
    b->dirty = 0;
    b->undo_recording = 1;
    E.cur_buf = slot;
    E.focus = FOCUS_EDITOR;
}

void cmd_autocomplete(char *buf, int *len, int *pos, int max_sz) {
    static const char *cmds[] = {
        "w", "wq", "q", "q!", "x", "e ", "new", "bd", "ot", "ht", "tree",
        "theme ", "themes", "set number", "set nonumber", "set cursorline",
        "set nocursorline", "set icons", "set noicons", "reload", "help", NULL
    };

    if (*len <= 0) return;
    for (int i = 0; cmds[i]; i++) {
        if (strncmp(cmds[i], buf, (size_t)*len) == 0 && (int)strlen(cmds[i]) < max_sz) {
            snprintf(buf, (size_t)max_sz, "%s", cmds[i]);
            *len = (int)strlen(buf);
            *pos = *len;
            return;
        }
    }
}

void cmd_execute(const char *raw) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", raw);

    char *cmd = buf;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    size_t clen = strlen(cmd);
    while (clen > 0 && isspace((unsigned char)cmd[clen - 1])) cmd[--clen] = '\0';
    if (!cmd[0]) return;

    /* Save in command history */
    if (E.cmd_hist_len == 0 || strcmp(E.cmd_history[E.cmd_hist_len - 1], cmd) != 0) {
        if (E.cmd_hist_len < 32) {
            snprintf(E.cmd_history[E.cmd_hist_len++], 512, "%s", cmd);
        } else {
            memmove(E.cmd_history[0], E.cmd_history[1], 31 * 512);
            snprintf(E.cmd_history[31], 512, "%s", cmd);
        }
    }

    Buffer *b = &E.buffers[E.cur_buf];

    /* Tab shortcuts :t1..:t9 and Russian :е1..:е9 */
    if ((cmd[0] == 't' && cmd[1] >= '1' && cmd[1] <= '9' && cmd[2] == '\0') ||
        (strncmp(cmd, "е", strlen("е")) == 0 && cmd[strlen("е")] >= '1' &&
         cmd[strlen("е")] <= '9' && cmd[strlen("е") + 1] == '\0')) {
        int num = (cmd[0] == 't') ? (cmd[1] - '0') : (cmd[strlen("е")] - '0');
        if (num >= 1 && num <= E.num_buffers) {
            E.cur_buf = num - 1;
            E.focus = FOCUS_EDITOR;
        } else {
            editor_set_status("No tab %d", num);
        }
        return;
    }

    /* Sidebar commands :ot / :ht / :tree (+ Russian :от / :нт) */
    if (strcmp(cmd, "ot") == 0 || strcmp(cmd, "Ot") == 0 || strcmp(cmd, "от") == 0) {
        E.sidebar_visible = 1;
        E.focus = FOCUS_SIDEBAR;
        tree_refresh();
        return;
    }
    if (strcmp(cmd, "ht") == 0 || strcmp(cmd, "Ht") == 0 || strcmp(cmd, "нт") == 0) {
        E.sidebar_visible = 0;
        E.focus = FOCUS_EDITOR;
        return;
    }
    if (strcmp(cmd, "tree") == 0 || strcmp(cmd, "Tree") == 0) {
        E.sidebar_visible = !E.sidebar_visible;
        E.focus = E.sidebar_visible ? FOCUS_SIDEBAR : FOCUS_EDITOR;
        if (E.sidebar_visible) tree_refresh();
        return;
    }

    if (strcmp(cmd, "q") == 0) {
        if (has_dirty_buffers()) {
            editor_set_status("Unsaved changes! Use :q! to force quit or :wq to save");
        } else {
            E.running = 0;
        }
        return;
    }
    if (strcmp(cmd, "q!") == 0 || strcmp(cmd, "qa!") == 0) {
        E.running = 0;
        return;
    }
    if (strcmp(cmd, "w") == 0) {
        buf_save(b, NULL);
        return;
    }
    if (strncmp(cmd, "w ", 2) == 0) {
        char *arg = cmd + 2;
        while (*arg && isspace((unsigned char)*arg)) arg++;
        buf_save(b, arg);
        return;
    }
    if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
        if (buf_save(b, NULL)) {
            if (!has_dirty_buffers()) E.running = 0;
        }
        return;
    }
    if (strcmp(cmd, "wq!") == 0) {
        if (buf_save(b, NULL)) E.running = 0;
        return;
    }
    if (strcmp(cmd, "bd") == 0 || strcmp(cmd, "c") == 0) {
        buf_close(E.cur_buf, 0);
        return;
    }
    if (strcmp(cmd, "bd!") == 0) {
        buf_close(E.cur_buf, 1);
        return;
    }
    if (strcmp(cmd, "bn") == 0) {
        E.cur_buf = (E.cur_buf + 1) % E.num_buffers;
        return;
    }
    if (strcmp(cmd, "bp") == 0) {
        E.cur_buf = (E.cur_buf - 1 + E.num_buffers) % E.num_buffers;
        return;
    }
    if (strcmp(cmd, "new") == 0 || strcmp(cmd, "enew") == 0) {
        if (E.num_buffers < MAX_BUFFERS) {
            buf_init_empty(&E.buffers[E.num_buffers]);
            E.cur_buf = E.num_buffers++;
            E.focus = FOCUS_EDITOR;
        }
        return;
    }
    if (strncmp(cmd, "e ", 2) == 0) {
        char *arg = cmd + 2;
        while (*arg && isspace((unsigned char)*arg)) arg++;
        if (*arg) {
            buf_open(arg);
            E.focus = FOCUS_EDITOR;
        }
        return;
    }
    if (strcmp(cmd, "themes") == 0) {
        editor_set_status("Themes: %s (use :theme <name>)", theme_list_names());
        return;
    }
    if (strncmp(cmd, "theme ", 6) == 0) {
        char *arg = cmd + 6;
        while (*arg && isspace((unsigned char)*arg)) arg++;
        if (theme_apply(arg, &E.theme)) {
            snprintf(E.cfg.theme, sizeof(E.cfg.theme), "%s", arg);
            editor_set_status("Theme applied: %s", arg);
        } else {
            editor_set_status("Unknown theme '%s'. Available: %s", arg, theme_list_names());
        }
        return;
    }
    if (strncmp(cmd, "set ", 4) == 0) {
        char *opt = cmd + 4;
        while (*opt && isspace((unsigned char)*opt)) opt++;
        if (strcmp(opt, "number") == 0 || strcmp(opt, "nu") == 0) {
            E.cfg.line_numbers = 1;
        } else if (strcmp(opt, "nonumber") == 0 || strcmp(opt, "nonu") == 0) {
            E.cfg.line_numbers = 0;
        } else if (strcmp(opt, "cursorline") == 0) {
            E.cfg.cursorline = 1;
        } else if (strcmp(opt, "nocursorline") == 0) {
            E.cfg.cursorline = 0;
        } else if (strcmp(opt, "icons") == 0) {
            E.cfg.nerd_icons = 1;
        } else if (strcmp(opt, "noicons") == 0) {
            E.cfg.nerd_icons = 0;
        } else if (strncmp(opt, "tabstop=", 8) == 0) {
            int n = atoi(opt + 8);
            if (n >= 1 && n <= 16) E.cfg.tab_size = n;
        } else {
            editor_set_status("Unknown option: %s", opt);
        }
        return;
    }
    if (strcmp(cmd, "reload") == 0) {
        config_load(&E.cfg);
        tree_refresh();
        editor_set_status("Reloaded catingiran.conf");
        return;
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
        open_help_buffer();
        return;
    }

    /* Jump to line number :<N> */
    int all_digits = 1;
    for (int i = 0; cmd[i]; i++) {
        if (!isdigit((unsigned char)cmd[i])) {
            all_digits = 0;
            break;
        }
    }
    if (all_digits) {
        int line_num = atoi(cmd);
        if (line_num < 1) line_num = 1;
        if (line_num > b->num_lines) line_num = b->num_lines;
        b->cy = line_num - 1;
        b->cx = 0;
        E.focus = FOCUS_EDITOR;
        return;
    }

    editor_set_status("Not an editor command: :%s", cmd);
}
