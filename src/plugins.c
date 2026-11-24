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

static void write_builtin_pack(const char *name, const char *dst) {
    mkdir_p(dst);
    char pconf[1200], wfile[1200];
    snprintf(pconf, sizeof(pconf), "%s/plugin.conf", dst);
    snprintf(wfile, sizeof(wfile), "%s/words.txt", dst);

    FILE *fc = fopen(pconf, "w");
    FILE *fw = fopen(wfile, "w");

    if (strcmp(name, "c") == 0 || strcmp(name, "cpp") == 0) {
        if (fc) {
            fprintf(fc,
                    "# C/C++ IDE Pack for catingiran\n"
                    "lang = c\n"
                    "command.cbuild = cc -Wall -Wextra -O2 *.c -o a.out && echo 'Build OK: ./a.out'\n"
                    "command.ccheck = cc -fsyntax-only -Wall -Wextra *.c && echo 'No syntax errors'\n"
                    "command.crun = cc -Wall -Wextra -O2 *.c -o /tmp/cti_crun && /tmp/cti_crun\n");
        }
        if (fw) {
            fprintf(fw,
                    "fopen()\nfclose()\nfgets()\nfread()\nfwrite()\n"
                    "getline()\nsprintf()\nsscanf()\natoi()\nstrtol()\n"
                    "strdup()\nstrcat()\nstrncat()\nqsort()\nbsearch()\n"
                    "assert()\nerrno\nstrerror()\nperror()\nexit()\n"
                    "EXIT_SUCCESS\nEXIT_FAILURE\ngetenv()\nstat()\n"
                    "opendir()\nreaddir()\nclosedir()\nfork()\nexecvp()\n"
                    "waitpid()\npopen()\npclose()\npthread_create()\n");
        }
    } else if (strcmp(name, "python") == 0 || strcmp(name, "py") == 0) {
        if (fc) {
            fprintf(fc,
                    "# Python IDE Pack for catingiran\n"
                    "lang = python\n"
                    "command.pyver = python3 --version\n"
                    "command.pytest = pytest -q\n"
                    "command.pycheck = python3 -m py_compile *.py && echo 'Python syntax OK'\n"
                    "command.pyvenv = python3 -m venv .venv && echo 'Created .venv'\n");
        }
        if (fw) {
            fprintf(fw,
                    "asyncio\npathlib\nPath()\njson\nre\nos\nsys\n"
                    "subprocess\ndataclass\ndataclasses\ntyping\n"
                    "Optional\nDict\nList\nTuple\nUnion\nAny\n"
                    "datetime\ncollections\ndefaultdict()\nCounter()\n"
                    "itertools\nfunctools\nlru_cache\n@property\n"
                    "@staticmethod\n@classmethod\n@dataclass\n"
                    "pytest\nrequests\nwith open() as f:\n");
        }
    } else if (strcmp(name, "java") == 0) {
        if (fc) {
            fprintf(fc,
                    "# Java IDE Pack for catingiran\n"
                    "lang = java\n"
                    "command.jver = java -version 2>&1\n"
                    "command.jbuild = javac *.java && echo 'Compiled .class files'\n"
                    "command.jclean = rm -f *.class && echo 'Cleaned .class files'\n");
        }
        if (fw) {
            fprintf(fw,
                    "StringBuilder()\nBufferedReader()\nInputStreamReader()\n"
                    "FileReader()\nFileWriter()\nIOException\n"
                    "RuntimeException\nCollections\nArrays\nStream\n"
                    "Collectors\nCompletableFuture\nThread\nRunnable\n"
                    "ConcurrentHashMap<>\nLinkedList<>\nTreeMap<>\n"
                    "equals()\nhashCode()\ntoString()\n");
        }
    } else if (strcmp(name, "web") == 0) {
        if (fc) {
            fprintf(fc,
                    "# Web JS/TS Pack for catingiran\n"
                    "lang = js\n"
                    "command.nver = node -v\n"
                    "command.npmtest = npm test\n");
        }
        if (fw) {
            fprintf(fw,
                    "console.log()\nfetch()\nPromise\nasync \nawait \n"
                    "JSON.stringify()\nJSON.parse()\ndocument.querySelector()\n"
                    "addEventListener()\nsetTimeout()\n");
        }
    } else if (strcmp(name, "git-extras") == 0) {
        if (fc) {
            fprintf(fc,
                    "# Git Extras Pack for catingiran\n"
                    "lang = all\n"
                    "command.glog = git log -n 15 --oneline --graph\n"
                    "command.gbranch = git branch -a\n"
                    "command.gpush = git push\n"
                    "command.gpull = git pull\n");
        }
    }

    if (fc) fclose(fc);
    if (fw) fclose(fw);
}

static void parse_plugin_words(const char *wpath, int default_lang) {
    FILE *f = fopen(wpath, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f) && E.num_plugin_words < MAX_PLUGIN_WORDS) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r' ||
                         line[l - 1] == ' ' || line[l - 1] == '\t')) {
            line[--l] = '\0';
        }
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        PluginWord *pw = &E.plugin_words[E.num_plugin_words++];
        snprintf(pw->word, sizeof(pw->word), "%s", p);
        snprintf(pw->kind, sizeof(pw->kind), "%s", "plug");
        pw->lang_id = default_lang;
    }
    fclose(f);
}

static int parse_plugin_conf(const char *plugin_name, const char *conf_path) {
    FILE *f = fopen(conf_path, "r");
    if (!f) return SYN_NONE;

    int lang_id = SYN_NONE;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        if (strncmp(p, "lang", 4) == 0 && strchr(p, '=')) {
            char *v = strchr(p, '=') + 1;
            while (*v == ' ' || *v == '\t') v++;
            lang_id = syntax_id_from_name(v);
        } else if (strncmp(p, "command.", 8) == 0) {
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
        } else if (strncmp(p, "theme", 5) == 0 && strchr(p, '=')) {
            char *t = strchr(p, '=') + 1;
            while (*t == ' ' || *t == '\t') t++;
            theme_apply(t, &E.theme);
        }
    }
    fclose(f);
    return lang_id;
}

void plugins_load_all(void) {
    char pdir[1024];
    plugins_get_dir(pdir, sizeof(pdir));
    mkdir_p(pdir);

    E.num_plugin_cmds = 0;
    E.num_plugin_words = 0;

    for (int i = 0; i < E.cfg.num_plugins; i++) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", pdir, E.cfg.plugins[i].name);
        E.cfg.plugins[i].installed = is_dir_present(path);

        if (E.cfg.plugins[i].installed) {
            char pconf[1400], wpath[1400];
            snprintf(pconf, sizeof(pconf), "%s/plugin.conf", path);
            snprintf(wpath, sizeof(wpath), "%s/words.txt", path);
            int lang_id = parse_plugin_conf(E.cfg.plugins[i].name, pconf);
            parse_plugin_words(wpath, lang_id);
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
        tools_append_output("Try:  :PlugAdd c   or   :PlugAdd python   or   :PlugAdd user/repo");
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

        if (ps->is_builtin) {
            write_builtin_pack(ps->name, dst);
            char msg[1500];
            snprintf(msg, sizeof(msg), "[INSTALLED] Built-in pack '%s' -> %s", ps->name, dst);
            tools_append_output(msg);
            ps->installed = 1;
            installed_cnt++;
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "[..] Cloning %s from %s", ps->name, ps->url);
            tools_append_output(msg);

            char gcmd[1600];
            snprintf(gcmd, sizeof(gcmd), "git clone --depth=1 '%s' '%s' 2>&1", ps->url, dst);
            run_git_pipe(gcmd);
            ps->installed = is_dir_present(dst);
            if (ps->installed) installed_cnt++;
        }
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

        if (ps->is_builtin) {
            write_builtin_pack(ps->name, dst);
            char msg[256];
            snprintf(msg, sizeof(msg), "[UPDATED] Built-in pack '%s'", ps->name);
            tools_append_output(msg);
            updated++;
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "[..] Updating %s", ps->name);
            tools_append_output(msg);

            char gcmd[1600];
            snprintf(gcmd, sizeof(gcmd), "git -C '%s' pull --ff-only 2>&1", dst);
            run_git_pipe(gcmd);
            updated++;
        }
    }

    if (updated == 0) {
        tools_append_output("No installed plugins to update.");
    }
    plugins_load_all();
    E.output_visible = 1;
    editor_set_status("PlugUpdate finished (%d plugins updated)", updated);
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
    ab_append_fmt(&out, "Plugins dir : %s\n", pdir);
    ab_append_fmt(&out, "Configured  : %d   |   Loaded Words: %d\n\n",
                  E.cfg.num_plugins, E.num_plugin_words);

    if (E.cfg.num_plugins == 0) {
        ab_append_str(&out, "No plugins configured yet.\n\n");
        ab_append_str(&out, "Install built-in language packs or GitHub repos:\n");
        ab_append_str(&out, "  :PlugAdd python       (Python stdlib + :pycheck/:pytest)\n");
        ab_append_str(&out, "  :PlugAdd c            (C/C++ stdlib + :cbuild/:crun)\n");
        ab_append_str(&out, "  :PlugAdd java         (Java classes + :jbuild/:jclean)\n");
        ab_append_str(&out, "  :PlugAdd user/repo    (Any GitHub plugin)\n");
        ab_append_str(&out, "  :PlugCreate myplug    (Create your own custom plugin)\n");
        ab_append_str(&out, "  :PlugList             (Full catalog)\n");
    } else {
        for (int i = 0; i < E.cfg.num_plugins; i++) {
            PluginSpec *ps = &E.cfg.plugins[i];
            ab_append_fmt(&out, " %2d. [%-9s] %-18s (%s)\n",
                          i + 1,
                          ps->installed ? "INSTALLED" : "MISSING",
                          ps->name,
                          ps->url);
        }
    }

    if (E.num_plugin_cmds > 0) {
        ab_append_str(&out, "\nRegistered Plugin Commands:\n");
        for (int i = 0; i < E.num_plugin_cmds; i++) {
            ab_append_fmt(&out, "  :%-16s -> %s (%s)\n",
                          E.plugin_cmds[i].name,
                          E.plugin_cmds[i].shell_cmd,
                          E.plugin_cmds[i].plugin_name);
        }
    }

    buf_open_scratch("[Plugins]", out.b ? out.b : "", 0);
    ab_free(&out);
}

void plugins_list_catalog(void) {
    const char *cat =
        "==============================================================\n"
        "         catingiran Official Plugin Catalog (:PlugList)\n"
        "==============================================================\n"
        "\n"
        "Built-in IDE Packs (install with :PlugAdd <name>):\n"
        "  python      Python stdlib/async autocomplete + :pycheck, :pytest, :pyvenv\n"
        "  c           C/C++ POSIX stdlib autocomplete + :cbuild, :ccheck, :crun\n"
        "  java        Java stdlib autocomplete + :jbuild, :jclean, :jver\n"
        "  web         JS/TS autocomplete + :nver, :npmtest\n"
        "  git-extras  Extra git commands (:glog, :gbranch, :gpush, :gpull)\n"
        "\n"
        "Custom & GitHub Plugins:\n"
        "  :PlugAdd user/repo   Clone & load plugin from GitHub\n"
        "  :PlugCreate <name>   Create your own custom plugin & edit plugin.conf\n"
        "  :PlugUpdate          Update installed plugins\n"
        "  :PlugClean           Remove unused plugins\n"
        "==============================================================\n";
    buf_open_scratch("[Plugin Catalog]", cat, 0);
}

void plugins_add(const char *spec) {
    if (!spec || !spec[0]) {
        editor_set_status("Usage: :PlugAdd <c|python|java|web|git-extras|user/repo>");
        return;
    }

    /* Check if already in config */
    for (int i = 0; i < E.cfg.num_plugins; i++) {
        if (strcmp(E.cfg.plugins[i].spec, spec) == 0 ||
            strcmp(E.cfg.plugins[i].name, spec) == 0) {
            plugins_install();
            return;
        }
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

void plugins_create(const char *name) {
    if (!name || !name[0]) {
        editor_set_status("Usage: :PlugCreate <plugin_name>");
        return;
    }

    char pdir[1024], dst[1200], pconf[1400], wfile[1400];
    plugins_get_dir(pdir, sizeof(pdir));
    snprintf(dst, sizeof(dst), "%s/%s", pdir, name);
    mkdir_p(dst);

    snprintf(pconf, sizeof(pconf), "%s/plugin.conf", dst);
    snprintf(wfile, sizeof(wfile), "%s/words.txt", dst);

    struct stat st;
    if (stat(pconf, &st) != 0) {
        FILE *fc = fopen(pconf, "w");
        if (fc) {
            fprintf(fc,
                    "# Custom plugin '%s' for catingiran\n"
                    "# Set lang = c | python | java | js | sh | all\n"
                    "lang = all\n"
                    "# Register custom Ex commands:\n"
                    "command.%s = echo 'Hello from %s plugin!'\n",
                    name, name, name);
            fclose(fc);
        }
    }
    if (stat(wfile, &st) != 0) {
        FILE *fw = fopen(wfile, "w");
        if (fw) {
            fprintf(fw, "# Add custom autocomplete words (one per line)\n");
            fclose(fw);
        }
    }

    /* Register in catingiran.conf if missing */
    int found = 0;
    for (int i = 0; i < E.cfg.num_plugins; i++) {
        if (strcmp(E.cfg.plugins[i].name, name) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) {
        char user_conf[1024];
        config_get_user_path(user_conf, sizeof(user_conf));
        FILE *f = fopen(user_conf, "a");
        if (f) {
            fprintf(f, "\nplugin = %s\n", name);
            fclose(f);
        }
    }

    config_load(&E.cfg);
    buf_open(pconf);
    editor_set_status("Created custom plugin '%s' — edit plugin.conf and run :reload", name);
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
