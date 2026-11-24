#include "state.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char *name;
    char *path;
    int is_dir;
} DirEntry;

static int cmp_entries(const void *a, const void *b) {
    const DirEntry *ea = (const DirEntry *)a;
    const DirEntry *eb = (const DirEntry *)b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return strcasecmp(ea->name, eb->name);
}

static void tree_push_node(const char *name, const char *path, int is_dir, int expanded, int depth) {
    if (E.tree_len >= E.tree_cap) {
        int ncap = E.tree_cap ? E.tree_cap * 2 : 64;
        TreeNode *nt = realloc(E.tree, (size_t)ncap * sizeof(TreeNode));
        if (!nt) return;
        E.tree = nt;
        E.tree_cap = ncap;
    }
    TreeNode *n = &E.tree[E.tree_len++];
    n->name = strdup(name);
    n->path = strdup(path);
    n->is_dir = is_dir;
    n->expanded = expanded;
    n->depth = depth;
}

static int path_in_list(const char *path, char **list, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(path, list[i]) == 0) return 1;
    }
    return 0;
}

static void scan_dir(const char *dir_path, int depth, char **expanded_paths, int exp_count) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    DirEntry *entries = NULL;
    int count = 0, cap = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (!E.cfg.show_hidden && ent->d_name[0] == '.')
            continue;
        if (strcmp(ent->d_name, ".git") == 0)
            continue;

        char full[2048];
        if (strcmp(dir_path, "/") == 0) {
            snprintf(full, sizeof(full), "/%s", ent->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        }

        struct stat st;
        int is_dir = 0;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = 1;
        }

        if (count >= cap) {
            cap = cap ? cap * 2 : 32;
            entries = realloc(entries, (size_t)cap * sizeof(DirEntry));
        }
        entries[count].name = strdup(ent->d_name);
        entries[count].path = strdup(full);
        entries[count].is_dir = is_dir;
        count++;
    }
    closedir(d);

    if (count > 1) {
        qsort(entries, (size_t)count, sizeof(DirEntry), cmp_entries);
    }

    for (int i = 0; i < count; i++) {
        int exp = entries[i].is_dir && path_in_list(entries[i].path, expanded_paths, exp_count);
        tree_push_node(entries[i].name, entries[i].path, entries[i].is_dir, exp, depth);
        if (exp) {
            scan_dir(entries[i].path, depth + 1, expanded_paths, exp_count);
        }
        free(entries[i].name);
        free(entries[i].path);
    }
    free(entries);
}

void tree_free(void) {
    for (int i = 0; i < E.tree_len; i++) {
        free(E.tree[i].name);
        free(E.tree[i].path);
    }
    free(E.tree);
    E.tree = NULL;
    E.tree_len = 0;
    E.tree_cap = 0;
}

void tree_refresh(void) {
    /* Remember expanded directories & selected path */
    char **exp_paths = NULL;
    int exp_count = 0;
    char *sel_path = (E.tree_sel >= 0 && E.tree_sel < E.tree_len)
                     ? strdup(E.tree[E.tree_sel].path) : NULL;

    for (int i = 0; i < E.tree_len; i++) {
        if (E.tree[i].is_dir && E.tree[i].expanded) {
            exp_paths = realloc(exp_paths, (size_t)(exp_count + 1) * sizeof(char *));
            exp_paths[exp_count++] = strdup(E.tree[i].path);
        }
    }

    tree_free();
    scan_dir(E.root_dir, 0, exp_paths, exp_count);

    for (int i = 0; i < exp_count; i++) free(exp_paths[i]);
    free(exp_paths);

    if (sel_path) {
        for (int i = 0; i < E.tree_len; i++) {
            if (strcmp(E.tree[i].path, sel_path) == 0) {
                E.tree_sel = i;
                break;
            }
        }
        free(sel_path);
    }

    if (E.tree_sel >= E.tree_len) {
        E.tree_sel = E.tree_len > 0 ? E.tree_len - 1 : 0;
    }
    if (E.tree_sel < 0) E.tree_sel = 0;
}

void tree_init(const char *root_path) {
    if (root_path && root_path[0]) {
        char *rp = realpath(root_path, NULL);
        if (rp) {
            snprintf(E.root_dir, sizeof(E.root_dir), "%s", rp);
            free(rp);
        } else {
            snprintf(E.root_dir, sizeof(E.root_dir), "%s", root_path);
        }
    } else {
        if (!getcwd(E.root_dir, sizeof(E.root_dir))) {
            snprintf(E.root_dir, sizeof(E.root_dir), ".");
        }
    }
    E.tree_sel = 0;
    E.tree_scroll = 0;
    tree_refresh();
}

void tree_toggle_selected(void) {
    if (E.tree_sel < 0 || E.tree_sel >= E.tree_len) return;
    TreeNode *n = &E.tree[E.tree_sel];
    if (n->is_dir) {
        n->expanded = !n->expanded;
        tree_refresh();
    } else {
        if (buf_open(n->path) >= 0) {
            E.focus = FOCUS_EDITOR;
        }
    }
}

void tree_collapse_or_parent(void) {
    if (E.tree_sel < 0 || E.tree_sel >= E.tree_len) return;
    TreeNode *n = &E.tree[E.tree_sel];
    if (n->is_dir && n->expanded) {
        n->expanded = 0;
        tree_refresh();
        return;
    }
    int cur_depth = n->depth;
    if (cur_depth == 0) return;
    for (int i = E.tree_sel - 1; i >= 0; i--) {
        if (E.tree[i].is_dir && E.tree[i].depth < cur_depth) {
            E.tree_sel = i;
            break;
        }
    }
}

void tree_create_prompt(void) {
    char name[256] = {0};
    editor_prompt_input("New file (end with / for dir): ", name, sizeof(name));
    if (!name[0]) return;

    const char *base_dir = E.root_dir;
    char parent_buf[1024];
    if (E.tree_sel >= 0 && E.tree_sel < E.tree_len) {
        TreeNode *n = &E.tree[E.tree_sel];
        if (n->is_dir) {
            base_dir = n->path;
        } else {
            snprintf(parent_buf, sizeof(parent_buf), "%s", n->path);
            char *sl = strrchr(parent_buf, '/');
            if (sl) {
                *sl = '\0';
                base_dir = parent_buf[0] ? parent_buf : "/";
            }
        }
    }

    size_t nl = strlen(name);
    int make_dir = (nl > 0 && name[nl - 1] == '/');
    if (make_dir) name[nl - 1] = '\0';

    char full[2048];
    snprintf(full, sizeof(full), "%s/%s", base_dir, name);

    if (make_dir) {
        if (mkdir(full, 0755) == 0) {
            editor_set_status("Created directory: %s", name);
        } else {
            editor_set_status("mkdir failed: %s", strerror(errno));
        }
    } else {
        FILE *f = fopen(full, "a");
        if (f) {
            fclose(f);
            buf_open(full);
            E.focus = FOCUS_EDITOR;
            editor_set_status("Created file: %s", name);
        } else {
            editor_set_status("Create failed: %s", strerror(errno));
        }
    }
    tree_refresh();
}

void tree_delete_selected(void) {
    if (E.tree_sel < 0 || E.tree_sel >= E.tree_len) return;
    TreeNode *n = &E.tree[E.tree_sel];

    char msg[320];
    snprintf(msg, sizeof(msg), "Delete '%s'? (y/n): ", n->name);
    char ans[16] = {0};
    editor_prompt_input(msg, ans, sizeof(ans));
    if (ans[0] != 'y' && ans[0] != 'Y') {
        editor_set_status("Delete cancelled");
        return;
    }

    int rc = n->is_dir ? rmdir(n->path) : unlink(n->path);
    if (rc == 0) {
        editor_set_status("Deleted: %s", n->name);
        tree_refresh();
    } else {
        editor_set_status("Delete error: %s", strerror(errno));
    }
}

void tree_rename_selected(void) {
    if (E.tree_sel < 0 || E.tree_sel >= E.tree_len) return;
    TreeNode *n = &E.tree[E.tree_sel];

    char new_name[256] = {0};
    editor_prompt_input("Rename to: ", new_name, sizeof(new_name));
    if (!new_name[0]) return;

    char dir_buf[1024];
    snprintf(dir_buf, sizeof(dir_buf), "%s", n->path);
    char *sl = strrchr(dir_buf, '/');
    if (sl) *sl = '\0';
    else snprintf(dir_buf, sizeof(dir_buf), ".");

    char dst[2048];
    snprintf(dst, sizeof(dst), "%s/%s", dir_buf[0] ? dir_buf : "/", new_name);

    if (rename(n->path, dst) == 0) {
        for (int i = 0; i < E.num_buffers; i++) {
            if (E.buffers[i].filename && strcmp(E.buffers[i].filename, n->path) == 0) {
                free(E.buffers[i].filename);
                E.buffers[i].filename = strdup(dst);
                E.buffers[i].syntax_id = syntax_detect(dst);
                syntax_update_all(&E.buffers[i]);
            }
        }
        editor_set_status("Renamed to: %s", new_name);
        tree_refresh();
    } else {
        editor_set_status("Rename error: %s", strerror(errno));
    }
}
