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
    const char *help_text =
        "==============================================================\n"
        "               catingiran (cti) v" CTI_VERSION " - Quick Help\n"
        "==============================================================\n"
        "\n"
        "COMMAND LINE (:)\n"
        "  :w [file]        Save current file (or save as)\n"
        "  :q / :q!         Quit (force quit without saving)\n"
        "  :wq / :x         Save and quit\n"
        "  :e <file>        Open file in new tab\n"
        "  :new / :bd       New empty tab / Close current tab\n"
        "  :conf            Edit ~/.config/catingiran/catingiran.conf\n"
        "  :reload          Reload catingiran.conf & plugins\n"
        "  :t1 .. :t9       Jump to tab 1..9 (also :e1..:e9 in RU)\n"
        "  :ot / :ht / :tree Open / Hide / Toggle file explorer\n"
        "  :vsplit / :split Vertical / Horizontal split (:only to close)\n"
        "  :run             Compile & run current C/C++/Python/Java/Sh file\n"
        "  :out / :ho       Show / Hide bottom output drawer\n"
        "  :term            Open interactive shell ($SHELL)\n"
        "  :find            Fuzzy quick-open file in project\n"
        "  :grep <text>     Search across project (Enter on line jumps)\n"
        "  :git / :diff     Show Git status/log or Git diff\n"
        "  :stats           Project code statistics & TODO count\n"
        "  :theme <name>    Switch theme (" "charcoal, obsidian, catppuccin, tokyonight, nord, light" ")\n"
        "  :set number      Line numbers (:set nonumber / icons / tabstop=N)\n"
        "\n"
        "PLUGIN MANAGER (catingiran Plug)\n"
        "  :PlugStatus      View configured & installed plugins\n"
        "  :PlugAdd u/repo  Add plugin to catingiran.conf & install\n"
        "  :PlugInstall     Clone plugins from catingiran.conf\n"
        "  :PlugUpdate      Update installed plugins (git pull)\n"
        "  :PlugClean       Remove plugins no longer in catingiran.conf\n"
        "\n"
        "KEYBINDINGS (NORMAL / VISUAL)\n"
        "  h / j / k / l    Move cursor (plus Russian layout support)\n"
        "  gcc / gc(vis)    Toggle line comment (// or #)\n"
        "  J / K (visual)   Move selected lines down / up\n"
        "  m / M            Toggle bookmark (★) / Jump to next bookmark\n"
        "  F5 / Space+r     Run current file\n"
        "  Ctrl+J           Toggle output drawer\n"
        "  Ctrl+P / Space+ff Fuzzy file finder\n"
        "  Ctrl+B / F2      Toggle file explorer sidebar\n"
        "  Ctrl+W           Switch active split pane\n"
        "  Ctrl+H / Ctrl+L  Focus sidebar / Focus code editor\n"
        "  Tab / Shift+Tab  Next / Previous tab\n"
        "  Space + 1..9     Jump to tab 1..9\n"
        "  Space + v / s / x Vertical split / Horizontal split / Close split\n"
        "  Space + e / w / q Toggle sidebar / Save / Quit\n"
        "==============================================================\n";

    buf_open_scratch("[Help]", help_text, 0);
}

void cmd_autocomplete(char *buf, int *len, int *pos, int max_sz) {
    static const char *cmds[] = {
        "w", "wq", "q", "q!", "x", "e ", "new", "bd", "ot", "ht", "tree",
        "conf", "reload", "run", "out", "ho", "term", "find", "grep ",
        "git", "diff", "stats", "vsplit", "split", "only",
        "PlugStatus", "PlugInstall", "PlugUpdate", "PlugClean", "PlugAdd ",
        "theme ", "themes", "set number", "set nonumber", "set cursorline",
        "set nocursorline", "set icons", "set noicons", "help", NULL
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

    /* Config file open */
    if (strcmp(cmd, "conf") == 0 || strcmp(cmd, "config") == 0) {
        char uconf[1024];
        config_get_user_path(uconf, sizeof(uconf));
        buf_open(uconf);
        E.focus = FOCUS_EDITOR;
        return;
    }

    /* Plugin manager commands */
    if (strcmp(cmd, "PlugInstall") == 0 || strcmp(cmd, "pluginstall") == 0) {
        plugins_install();
        return;
    }
    if (strcmp(cmd, "PlugUpdate") == 0 || strcmp(cmd, "plugupdate") == 0) {
        plugins_update();
        return;
    }
    if (strcmp(cmd, "PlugClean") == 0 || strcmp(cmd, "plugclean") == 0) {
        plugins_clean();
        return;
    }
    if (strcmp(cmd, "PlugStatus") == 0 || strcmp(cmd, "plugstatus") == 0) {
        plugins_status();
        return;
    }
    if (strncmp(cmd, "PlugAdd ", 8) == 0) {
        char *arg = cmd + 8;
        while (*arg && isspace((unsigned char)*arg)) arg++;
        plugins_add(arg);
        return;
    }

    /* IDE Tools */
    if (strcmp(cmd, "run") == 0) {
        tools_run_current();
        return;
    }
    if (strcmp(cmd, "out") == 0) {
        E.output_visible = 1;
        return;
    }
    if (strcmp(cmd, "ho") == 0) {
        E.output_visible = 0;
        return;
    }
    if (strcmp(cmd, "term") == 0 || strcmp(cmd, "sh") == 0) {
        tools_spawn_shell();
        return;
    }
    if (strcmp(cmd, "find") == 0) {
        tools_fuzzy_find();
        return;
    }
    if (strncmp(cmd, "grep ", 5) == 0) {
        char *arg = cmd + 5;
        while (*arg && isspace((unsigned char)*arg)) arg++;
        tools_grep(arg);
        return;
    }
    if (strcmp(cmd, "git") == 0 || strcmp(cmd, "gs") == 0) {
        tools_git_status();
        return;
    }
    if (strcmp(cmd, "diff") == 0) {
        tools_git_diff();
        return;
    }
    if (strcmp(cmd, "stats") == 0) {
        tools_project_stats();
        return;
    }
    if (strcmp(cmd, "mark") == 0) {
        tools_toggle_bookmark(b, b->cy);
        return;
    }
    if (strcmp(cmd, "dup") == 0) {
        tools_duplicate_line(b);
        return;
    }

    /* Split panes */
    if (strcmp(cmd, "vsplit") == 0 || strcmp(cmd, "vs") == 0) {
        if (E.split_mode == SPLIT_NONE) {
            E.split_buf = (E.num_buffers > 1) ? ((E.cur_buf + 1) % E.num_buffers) : E.cur_buf;
            E.active_pane = 0;
        }
        E.split_mode = SPLIT_VERT;
        return;
    }
    if (strcmp(cmd, "split") == 0 || strcmp(cmd, "sp") == 0) {
        if (E.split_mode == SPLIT_NONE) {
            E.split_buf = (E.num_buffers > 1) ? ((E.cur_buf + 1) % E.num_buffers) : E.cur_buf;
            E.active_pane = 0;
        }
        E.split_mode = SPLIT_HORIZ;
        return;
    }
    if (strcmp(cmd, "only") == 0 || strcmp(cmd, "close") == 0) {
        E.split_mode = SPLIT_NONE;
        E.active_pane = 0;
        return;
    }

    if (strcmp(cmd, "q") == 0) {
        if (E.split_mode != SPLIT_NONE) {
            E.split_mode = SPLIT_NONE;
            E.active_pane = 0;
            return;
        }
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
        tools_refresh_git_branch();
        editor_set_status("Reloaded catingiran.conf & plugins");
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

    /* Custom commands registered by plugins */
    if (plugins_try_command(cmd)) return;

    editor_set_status("Not an editor command: :%s", cmd);
}
