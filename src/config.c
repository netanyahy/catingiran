#include "state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static Color rgb(unsigned char r, unsigned char g, unsigned char b) {
    Color c = { r, g, b };
    return c;
}

static const Theme BUILTIN_THEMES[] = {
    /* 1. Charcoal (OnVim-inspired dark graphite monochrome) */
    {
        .name = "charcoal",
        .bg = {22, 23, 27},
        .fg = {224, 226, 232},
        .gutter_fg = {86, 91, 104},
        .gutter_bg = {22, 23, 27},
        .cursorline_bg = {32, 34, 41},
        .selection_bg = {58, 63, 75},
        .sidebar_bg = {17, 18, 22},
        .sidebar_fg = {184, 188, 198},
        .sidebar_sel_bg = {45, 48, 58},
        .sidebar_sel_fg = {255, 255, 255},
        .sidebar_dir_fg = {235, 238, 244},
        .border_fg = {44, 47, 56},
        .tab_bg = {18, 19, 23},
        .tab_fg = {120, 126, 140},
        .tab_active_bg = {45, 48, 58},
        .tab_active_fg = {255, 255, 255},
        .status_bg = {28, 30, 36},
        .status_fg = {210, 214, 222},
        .status_mode_bg = {230, 232, 238},
        .status_mode_fg = {18, 19, 23},
        .hl_comment = {104, 110, 124},
        .hl_kw1 = {245, 245, 250},
        .hl_kw2 = {180, 192, 208},
        .hl_string = {175, 196, 174},
        .hl_number = {212, 184, 152},
        .hl_preproc = {188, 174, 204},
        .hl_func = {174, 198, 224},
        .hl_match = {255, 215, 110}
    },
    /* 2. Obsidian (Pitch black + Gentoo violet accent) */
    {
        .name = "obsidian",
        .bg = {10, 10, 13},
        .fg = {240, 240, 245},
        .gutter_fg = {82, 84, 98},
        .gutter_bg = {10, 10, 13},
        .cursorline_bg = {20, 20, 26},
        .selection_bg = {52, 46, 74},
        .sidebar_bg = {8, 8, 10},
        .sidebar_fg = {176, 178, 192},
        .sidebar_sel_bg = {36, 34, 48},
        .sidebar_sel_fg = {255, 255, 255},
        .sidebar_dir_fg = {156, 124, 244},
        .border_fg = {32, 32, 42},
        .tab_bg = {8, 8, 10},
        .tab_fg = {106, 108, 124},
        .tab_active_bg = {255, 255, 255},
        .tab_active_fg = {8, 8, 10},
        .status_bg = {16, 16, 21},
        .status_fg = {220, 222, 230},
        .status_mode_bg = {156, 124, 244},
        .status_mode_fg = {10, 10, 13},
        .hl_comment = {96, 98, 116},
        .hl_kw1 = {156, 124, 244},
        .hl_kw2 = {210, 212, 224},
        .hl_string = {152, 212, 170},
        .hl_number = {232, 184, 134},
        .hl_preproc = {212, 160, 232},
        .hl_func = {196, 178, 255},
        .hl_match = {255, 210, 90}
    },
    /* 3. Catppuccin Mocha */
    {
        .name = "catppuccin",
        .bg = {30, 30, 46},
        .fg = {205, 214, 244},
        .gutter_fg = {108, 112, 134},
        .gutter_bg = {30, 30, 46},
        .cursorline_bg = {42, 43, 61},
        .selection_bg = {69, 71, 90},
        .sidebar_bg = {24, 24, 37},
        .sidebar_fg = {186, 194, 222},
        .sidebar_sel_bg = {49, 50, 68},
        .sidebar_sel_fg = {205, 214, 244},
        .sidebar_dir_fg = {137, 180, 250},
        .border_fg = {49, 50, 68},
        .tab_bg = {24, 24, 37},
        .tab_fg = {127, 132, 156},
        .tab_active_bg = {49, 50, 68},
        .tab_active_fg = {205, 214, 244},
        .status_bg = {24, 24, 37},
        .status_fg = {205, 214, 244},
        .status_mode_bg = {203, 166, 247},
        .status_mode_fg = {24, 24, 37},
        .hl_comment = {108, 112, 134},
        .hl_kw1 = {203, 166, 247},
        .hl_kw2 = {249, 226, 175},
        .hl_string = {166, 227, 161},
        .hl_number = {250, 179, 135},
        .hl_preproc = {137, 180, 250},
        .hl_func = {137, 220, 235},
        .hl_match = {249, 226, 175}
    },
    /* 4. Tokyo Night */
    {
        .name = "tokyonight",
        .bg = {26, 27, 38},
        .fg = {192, 202, 245},
        .gutter_fg = {86, 95, 137},
        .gutter_bg = {26, 27, 38},
        .cursorline_bg = {36, 40, 59},
        .selection_bg = {51, 70, 124},
        .sidebar_bg = {22, 22, 30},
        .sidebar_fg = {169, 177, 214},
        .sidebar_sel_bg = {41, 46, 66},
        .sidebar_sel_fg = {192, 202, 245},
        .sidebar_dir_fg = {122, 162, 247},
        .border_fg = {41, 46, 66},
        .tab_bg = {22, 22, 30},
        .tab_fg = {86, 95, 137},
        .tab_active_bg = {41, 46, 66},
        .tab_active_fg = {192, 202, 245},
        .status_bg = {22, 22, 30},
        .status_fg = {169, 177, 214},
        .status_mode_bg = {122, 162, 247},
        .status_mode_fg = {22, 22, 30},
        .hl_comment = {86, 95, 137},
        .hl_kw1 = {187, 154, 247},
        .hl_kw2 = {125, 207, 255},
        .hl_string = {158, 206, 106},
        .hl_number = {255, 158, 100},
        .hl_preproc = {122, 162, 247},
        .hl_func = {122, 162, 247},
        .hl_match = {224, 175, 104}
    },
    /* 5. Nord */
    {
        .name = "nord",
        .bg = {46, 52, 64},
        .fg = {216, 222, 233},
        .gutter_fg = {97, 110, 136},
        .gutter_bg = {46, 52, 64},
        .cursorline_bg = {59, 66, 82},
        .selection_bg = {67, 76, 94},
        .sidebar_bg = {40, 46, 56},
        .sidebar_fg = {216, 222, 233},
        .sidebar_sel_bg = {67, 76, 94},
        .sidebar_sel_fg = {236, 239, 244},
        .sidebar_dir_fg = {136, 192, 208},
        .border_fg = {59, 66, 82},
        .tab_bg = {40, 46, 56},
        .tab_fg = {129, 142, 166},
        .tab_active_bg = {67, 76, 94},
        .tab_active_fg = {236, 239, 244},
        .status_bg = {59, 66, 82},
        .status_fg = {216, 222, 233},
        .status_mode_bg = {136, 192, 208},
        .status_mode_fg = {46, 52, 64},
        .hl_comment = {97, 110, 136},
        .hl_kw1 = {129, 161, 193},
        .hl_kw2 = {143, 188, 187},
        .hl_string = {163, 190, 140},
        .hl_number = {180, 142, 173},
        .hl_preproc = {136, 192, 208},
        .hl_func = {136, 192, 208},
        .hl_match = {235, 203, 139}
    },
    /* 6. Light */
    {
        .name = "light",
        .bg = {246, 248, 250},
        .fg = {36, 41, 47},
        .gutter_fg = {140, 149, 159},
        .gutter_bg = {246, 248, 250},
        .cursorline_bg = {234, 238, 242},
        .selection_bg = {208, 215, 222},
        .sidebar_bg = {238, 241, 244},
        .sidebar_fg = {50, 56, 62},
        .sidebar_sel_bg = {208, 215, 222},
        .sidebar_sel_fg = {10, 12, 16},
        .sidebar_dir_fg = {9, 105, 218},
        .border_fg = {208, 215, 222},
        .tab_bg = {234, 238, 242},
        .tab_fg = {100, 108, 118},
        .tab_active_bg = {255, 255, 255},
        .tab_active_fg = {24, 24, 28},
        .status_bg = {228, 232, 236},
        .status_fg = {36, 41, 47},
        .status_mode_bg = {36, 41, 47},
        .status_mode_fg = {255, 255, 255},
        .hl_comment = {110, 119, 129},
        .hl_kw1 = {207, 34, 46},
        .hl_kw2 = {130, 80, 223},
        .hl_string = {17, 99, 41},
        .hl_number = {5, 80, 174},
        .hl_preproc = {149, 56, 0},
        .hl_func = {9, 105, 218},
        .hl_match = {255, 220, 110}
    }
};

#define NUM_THEMES (sizeof(BUILTIN_THEMES) / sizeof(BUILTIN_THEMES[0]))

int theme_apply(const char *name, Theme *out) {
    (void)rgb;
    for (size_t i = 0; i < NUM_THEMES; i++) {
        if (strcmp(BUILTIN_THEMES[i].name, name) == 0) {
            *out = BUILTIN_THEMES[i];
            return 1;
        }
    }
    return 0;
}

const char *theme_list_names(void) {
    return "charcoal, obsidian, catppuccin, tokyonight, nord, light";
}

void config_init_defaults(Config *cfg) {
    snprintf(cfg->theme, sizeof(cfg->theme), "%s", "charcoal");
    cfg->tab_size = 4;
    cfg->expand_tab = 1;
    cfg->line_numbers = 1;
    cfg->cursorline = 1;
    cfg->auto_indent = 1;
    cfg->auto_pairs = 1;
    cfg->mouse_enabled = 1;
    cfg->sidebar_open_on_start = 1;
    cfg->sidebar_width = 28;
    cfg->show_hidden = 1;
    cfg->nerd_icons = 1;
    cfg->russian_keymap = 1;
    cfg->show_tabs = 1;
    cfg->output_height = 8;
    cfg->git_branch = 1;
    cfg->num_plugins = 0;
}

static char *trim_ws(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

static int parse_bool(const char *v) {
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 ||
        strcmp(v, "yes") == 0 || strcmp(v, "on") == 0) {
        return 1;
    }
    return 0;
}

static void mkdir_p_one(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}

void config_get_user_path(char *buf, size_t sz) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0]) {
        snprintf(buf, sz, "%s/catingiran/catingiran.conf", xdg);
    } else if (home && home[0]) {
        snprintf(buf, sz, "%s/.config/catingiran/catingiran.conf", home);
    } else {
        snprintf(buf, sz, "catingiran.conf");
    }
}

static void add_plugin_spec(Config *cfg, const char *val) {
    if (!val || !val[0] || cfg->num_plugins >= MAX_PLUGINS) return;
    PluginSpec *ps = &cfg->plugins[cfg->num_plugins];
    snprintf(ps->spec, sizeof(ps->spec), "%s", val);

    if (strncmp(val, "http://", 7) == 0 || strncmp(val, "https://", 8) == 0 ||
        strncmp(val, "git@", 4) == 0) {
        snprintf(ps->url, sizeof(ps->url), "%s", val);
    } else {
        snprintf(ps->url, sizeof(ps->url), "https://github.com/%s.git", val);
    }

    const char *sl = strrchr(val, '/');
    const char *nm = sl ? sl + 1 : val;
    snprintf(ps->name, sizeof(ps->name), "%s", nm);
    size_t nl = strlen(ps->name);
    if (nl > 4 && strcmp(ps->name + nl - 4, ".git") == 0) {
        ps->name[nl - 4] = '\0';
    }
    ps->installed = 0;
    cfg->num_plugins++;
}

static void copy_preset_if_missing(const char *user_path) {
    struct stat st;
    if (stat(user_path, &st) == 0) return;

    const char *candidates[] = {
        SYSCONFDIR "/catingiran.conf",
        DATADIR "/catingiran.conf",
        "./catingiran.conf"
    };

    FILE *src = NULL;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        src = fopen(candidates[i], "r");
        if (src) break;
    }

    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char dir[1024];
    if (xdg && xdg[0]) {
        snprintf(dir, sizeof(dir), "%s", xdg);
        mkdir_p_one(dir);
        snprintf(dir, sizeof(dir), "%s/catingiran", xdg);
        mkdir_p_one(dir);
    } else if (home && home[0]) {
        snprintf(dir, sizeof(dir), "%s/.config", home);
        mkdir_p_one(dir);
        snprintf(dir, sizeof(dir), "%s/.config/catingiran", home);
        mkdir_p_one(dir);
    }

    FILE *dst = fopen(user_path, "w");
    if (dst) {
        if (src) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                fwrite(buf, 1, n, dst);
            }
        } else {
            fprintf(dst,
                    "# catingiran (cti) configuration\n"
                    "theme = charcoal\n"
                    "tab_size = 4\n"
                    "expand_tab = true\n"
                    "line_numbers = true\n"
                    "cursorline = true\n"
                    "auto_indent = true\n"
                    "auto_pairs = true\n"
                    "show_tabs = true\n"
                    "mouse = true\n"
                    "sidebar_open = true\n"
                    "sidebar_width = 28\n"
                    "show_hidden = true\n"
                    "nerd_icons = true\n"
                    "output_height = 8\n"
                    "git_branch = true\n"
                    "russian_keymap = true\n"
                    "# plugin = user/repo\n");
        }
        fclose(dst);
    }
    if (src) fclose(src);
}

static void parse_conf_file(const char *path, Config *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim_ws(line);
        if (*p == '\0' || *p == '#' || *p == ';') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = trim_ws(p);
        char *val = trim_ws(eq + 1);

        /* Strip optional inline comment and quotes */
        if (*val == '"' || *val == '\'') {
            char q = *val++;
            char *endq = strchr(val, q);
            if (endq) *endq = '\0';
        } else {
            char *hash = strchr(val, '#');
            if (hash) *hash = '\0';
            val = trim_ws(val);
        }

        if (strcmp(key, "theme") == 0) {
            snprintf(cfg->theme, sizeof(cfg->theme), "%s", val);
        } else if (strcmp(key, "tab_size") == 0) {
            int n = atoi(val);
            if (n >= 1 && n <= 16) cfg->tab_size = n;
        } else if (strcmp(key, "expand_tab") == 0) {
            cfg->expand_tab = parse_bool(val);
        } else if (strcmp(key, "line_numbers") == 0) {
            cfg->line_numbers = parse_bool(val);
        } else if (strcmp(key, "cursorline") == 0) {
            cfg->cursorline = parse_bool(val);
        } else if (strcmp(key, "auto_indent") == 0) {
            cfg->auto_indent = parse_bool(val);
        } else if (strcmp(key, "auto_pairs") == 0) {
            cfg->auto_pairs = parse_bool(val);
        } else if (strcmp(key, "mouse") == 0) {
            cfg->mouse_enabled = parse_bool(val);
        } else if (strcmp(key, "sidebar_open") == 0) {
            cfg->sidebar_open_on_start = parse_bool(val);
        } else if (strcmp(key, "sidebar_width") == 0) {
            int n = atoi(val);
            if (n >= 16 && n <= 60) cfg->sidebar_width = n;
        } else if (strcmp(key, "show_hidden") == 0) {
            cfg->show_hidden = parse_bool(val);
        } else if (strcmp(key, "nerd_icons") == 0) {
            cfg->nerd_icons = parse_bool(val);
        } else if (strcmp(key, "russian_keymap") == 0) {
            cfg->russian_keymap = parse_bool(val);
        } else if (strcmp(key, "show_tabs") == 0) {
            cfg->show_tabs = parse_bool(val);
        } else if (strcmp(key, "output_height") == 0) {
            int n = atoi(val);
            if (n >= 4 && n <= 30) cfg->output_height = n;
        } else if (strcmp(key, "git_branch") == 0) {
            cfg->git_branch = parse_bool(val);
        } else if (strcmp(key, "plugin") == 0) {
            add_plugin_spec(cfg, val);
        }
    }
    fclose(f);
}

void config_load(Config *cfg) {
    config_init_defaults(cfg);

    char user_conf[1024];
    config_get_user_path(user_conf, sizeof(user_conf));
    copy_preset_if_missing(user_conf);

    /* Load system preset first, then user config overrides */
    parse_conf_file(SYSCONFDIR "/catingiran.conf", cfg);
    parse_conf_file(user_conf, cfg);

    if (!theme_apply(cfg->theme, &E.theme)) {
        theme_apply("charcoal", &E.theme);
    }
    plugins_load_all();
}
