#include "state.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

void plugins_get_dir(char *buf, size_t sz) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0]) {
        snprintf(buf, sz, "%s/catingiran/plugins", xdg);
    } else if (home && home[0]) {
        snprintf(buf, sz, "%s/.config/catingiran/plugins", home);
    } else {
        snprintf(buf, sz, "plugins");
    }
}

static int is_dir_present(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void parse_plugin_conf(const char *plugin_name, const char *conf_path) {
    FILE *f = fopen(conf_path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        /* Format: command.<name> = <shell command> */
        if (strncmp(p, "command.", 8) == 0) {
            char *eq = strchr(p + 8, '=');
            if (!eq) continue;
            *eq = '\0';
            char *cname = p + 8;
            char *cend = cname + strlen(cname);
            while (cend > cname && (cend[-1] == ' ' || cend[-1] == '\t')) *--cend = '\0';

            char *cval = eq + 1;
            while (*cval == ' ' || *cval == '\t') cval++;

            if (cname[0] && cval[0] && E.num_plugin_cmds < MAX_PLUGIN_CMDS) {
                PluginCommand *pc = &E.plugin_cmds[E.num_plugin_cmds++];
                snprintf(pc->name, sizeof(pc->name), "%s", cname);
                snprintf(pc->shell_cmd, sizeof(pc->shell_cmd), "%s", cval);
                snprintf(pc->plugin_name, sizeof(pc->plugin_name), "%s", plugin_name);
            }
        } else if (strncmp(p, "theme =", 7) == 0 || strncmp(p, "theme=", 6) == 0) {
            char *eq = strchr(p, '=');
            if (eq) {
                char *t = eq + 1;
                while (*t == ' ' || *t == '\t') t++;
                theme_apply(t, &E.theme);
            }
        }
    }
    fclose(f);
}

void plugins_load_all(void) {
    char pdir[1024];
    plugins_get_dir(pdir, sizeof(pdir));
    mkdir_p(pdir);

    E.num_plugin_cmds = 0;

    for (int i = 0; i < E.cfg.num_plugins; i++) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", pdir, E.cfg.plugins[i].name);
        E.cfg.plugins[i].installed = is_dir_present(path);

        if (E.cfg.plugins[i].installed) {
            char pconf[1400];
            snprintf(pconf, sizeof(pconf), "%s/plugin.conf", path);
            parse_plugin_conf(E.cfg.plugins[i].name, pconf);
        }
    }
}

static void run_git_pipe(const char *cmd_str) {
    FILE *fp = popen(cmd_str, "r");
    if (!fp) {
        tools_append_output("Failed to run git command");
        return;
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        size_t l = strlen(buf);
        while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = '\0';
        tools_append_output(buf);
    }
    pclose(fp);
}

void plugins_install(void) {
    char pdir[1024];
    plugins_get_dir(pdir, sizeof(pdir));
    mkdir_p(pdir);

    tools_clear_output("PlugInstall");
    if (E.cfg.num_plugins == 0) {
        tools_append_output("No plugins configured in catingiran.conf.");
        tools_append_output("Add:  plugin = user/repo   or use :PlugAdd user/repo");
        E.output_visible = 1;
        return;
    }

    int installed_cnt = 0;
    for (int i = 0; i < E.cfg.num_plugins; i++) {
        PluginSpec *ps = &E.cfg.plugins[i];
        char dst[1200];
        snprintf(dst, sizeof(dst), "%s/%s", pdir, ps->name);
        if (is_dir_present(dst)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "[OK] %s already installed", ps->name);
            tools_append_output(msg);
            ps->installed = 1;
            continue;
        }
        char msg[256];
        snprintf(msg, sizeof(msg), "[..] Cloning %s from %s", ps->name, ps->url);
        tools_append_output(msg);

        char gcmd[1600];
        snprintf(gcmd, sizeof(gcmd), "git clone --depth=1 '%s' '%s' 2>&1", ps->url, dst);
        run_git_pipe(gcmd);
        ps->installed = is_dir_present(dst);
        if (ps->installed) installed_cnt++;
    }

    plugins_load_all();
    E.output_visible = 1;
    editor_set_status("PlugInstall finished (%d newly installed)", installed_cnt);
}

void plugins_update(void) {
    char pdir[1024];
    plugins_get_dir(pdir, sizeof(pdir));

    tools_clear_output("PlugUpdate");
    int updated = 0;
    for (int i = 0; i < E.cfg.num_plugins; i++) {
        PluginSpec *ps = &E.cfg.plugins[i];
        char dst[1200];
        snprintf(dst, sizeof(dst), "%s/%s", pdir, ps->name);
        if (!is_dir_present(dst)) continue;

        char msg[256];
        snprintf(msg, sizeof(msg), "[..] Updating %s", ps->name);
        tools_append_output(msg);

        char gcmd[1600];
        snprintf(gcmd, sizeof(gcmd), "git -C '%s' pull --ff-only 2>&1", dst);
        run_git_pipe(gcmd);
        updated++;
    }

    if (updated == 0) {
        tools_append_output("No installed plugins to update.");
    }
    plugins_load_all();
    E.output_visible = 1;
    editor_set_status("PlugUpdate finished (%d plugins checked)", updated);
}

void plugins_clean(void) {
    char pdir[1024];
    plugins_get_dir(pdir, sizeof(pdir));

    DIR *d = opendir(pdir);
    if (!d) return;

    tools_clear_output("PlugClean");
    int removed = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        int keep = 0;
        for (int i = 0; i < E.cfg.num_plugins; i++) {
            if (strcmp(E.cfg.plugins[i].name, ent->d_name) == 0) {
                keep = 1;
                break;
            }
        }
        if (!keep) {
            char cmd[1500];
            snprintf(cmd, sizeof(cmd), "rm -rf '%s/%s'", pdir, ent->d_name);
            int rc = system(cmd);
            (void)rc;
            char msg[256];
            snprintf(msg, sizeof(msg), "[REMOVED] %s", ent->d_name);
            tools_append_output(msg);
            removed++;
        }
    }
    closedir(d);

    if (removed == 0) tools_append_output("Everything is clean.");
    plugins_load_all();
    E.output_visible = 1;
    editor_set_status("PlugClean removed %d unused plugin(s)", removed);
}

void plugins_status(void) {
    plugins_load_all();
    char pdir[1024];
    plugins_get_dir(pdir, sizeof(pdir));

    Abuf out = {NULL, 0, 0};
    ab_append_str(&out, "==============================================================\n");
    ab_append_str(&out, "            catingiran Plugin Manager (:PlugStatus)\n");
    ab_append_str(&out, "==============================================================\n");
    ab_append_fmt(&out, "Plugins dir: %s\n", pdir);
    ab_append_fmt(&out, "Configured : %d\n\n", E.cfg.num_plugins);

    if (E.cfg.num_plugins == 0) {
        ab_append_str(&out, "No plugins configured yet.\n\n");
        ab_append_str(&out, "Add plugins in ~/.config/catingiran/catingiran.conf:\n");
        ab_append_str(&out, "  plugin = user/repo\n\n");
        ab_append_str(&out, "Or run directly:\n");
        ab_append_str(&out, "  :PlugAdd user/repo\n");
        ab_append_str(&out, "  :PlugInstall\n");
    } else {
        for (int i = 0; i < E.cfg.num_plugins; i++) {
            PluginSpec *ps = &E.cfg.plugins[i];
            ab_append_fmt(&out, " %2d. [%-9s] %-24s (%s)\n",
                          i + 1,
                          ps->installed ? "INSTALLED" : "MISSING",
                          ps->name,
                          ps->url);
        }
    }

    if (E.num_plugin_cmds > 0) {
        ab_append_str(&out, "\nRegistered Plugin Commands:\n");
        for (int i = 0; i < E.num_plugin_cmds; i++) {
            ab_append_fmt(&out, "  :%-18s -> %s (%s)\n",
                          E.plugin_cmds[i].name,
                          E.plugin_cmds[i].shell_cmd,
                          E.plugin_cmds[i].plugin_name);
        }
    }

    buf_open_scratch("[Plugins]", out.b ? out.b : "", 0);
    ab_free(&out);
}

void plugins_add(const char *spec) {
    if (!spec || !spec[0]) {
        editor_set_status("Usage: :PlugAdd user/repo");
        return;
    }

    char user_conf[1024];
    config_get_user_path(user_conf, sizeof(user_conf));
    FILE *f = fopen(user_conf, "a");
    if (f) {
        fprintf(f, "\nplugin = %s\n", spec);
        fclose(f);
    }

    config_load(&E.cfg);
    plugins_install();
}

int plugins_try_command(const char *cmd) {
    for (int i = 0; i < E.num_plugin_cmds; i++) {
        if (strcmp(E.plugin_cmds[i].name, cmd) == 0) {
            tools_clear_output(cmd);
            FILE *fp = popen(E.plugin_cmds[i].shell_cmd, "r");
            if (fp) {
                char buf[256];
                while (fgets(buf, sizeof(buf), fp)) {
                    size_t l = strlen(buf);
                    while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = '\0';
                    tools_append_output(buf);
                }
                pclose(fp);
            }
            E.output_visible = 1;
            return 1;
        }
    }
    return 0;
}
