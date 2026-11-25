#include "state.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void build_uri(const char *path, char *out, size_t sz) {
    char *rp = realpath(path, NULL);
    if (rp) {
        snprintf(out, sz, "file://%s", rp);
        free(rp);
    } else if (path[0] == '/') {
        snprintf(out, sz, "file://%s", path);
    } else {
        snprintf(out, sz, "file://%s/%s", E.root_dir, path);
    }
}

static void uri_to_path(const char *uri, char *out, size_t sz) {
    if (strncmp(uri, "file://", 7) == 0) {
        snprintf(out, sz, "%s", uri + 7);
    } else {
        snprintf(out, sz, "%s", uri);
    }
}

static const char *lsp_lang_str(int syn) {
    switch (syn) {
        case SYN_C:      return "c";
        case SYN_PYTHON: return "python";
        case SYN_JAVA:   return "java";
        default:         return "plaintext";
    }
}

static char *buf_full_text_json(Buffer *b) {
    Abuf raw = {NULL, 0, 0};
    for (int i = 0; i < b->num_lines; i++) {
        if (b->lines[i].size > 0) ab_append(&raw, b->lines[i].chars, b->lines[i].size);
        ab_append(&raw, "\n", 1);
    }
    ab_append(&raw, "\0", 1);
    char *esc = json_escape(raw.b ? raw.b : "");
    ab_free(&raw);
    return esc;
}

static int lsp_send(LspServer *srv, const char *json_body) {
    if (!srv || !srv->active || srv->fd_in < 0 || !json_body) return -1;
    int blen = (int)strlen(json_body);
    char hdr[64];
    int hlen = snprintf(hdr, sizeof(hdr), "Content-Length: %d\r\n\r\n", blen);

    /* Write header + body */
    ssize_t w1 = write(srv->fd_in, hdr, (size_t)hlen);
    ssize_t total = 0;
    while (total < blen) {
        ssize_t w2 = write(srv->fd_in, json_body + total, (size_t)(blen - total));
        if (w2 <= 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            return -1;
        }
        total += w2;
    }
    return (w1 > 0 && total == blen) ? 0 : -1;
}

static int bin_in_path(const char *name) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    return system(cmd) == 0;
}

static LspServer *find_or_start_server(int syn) {
    if (!E.cfg.lsp_enabled) return NULL;
    if (syn != SYN_C && syn != SYN_PYTHON && syn != SYN_JAVA) return NULL;

    for (int i = 0; i < MAX_LSP_SERVERS; i++) {
        if (E.lsp[i].active && E.lsp[i].lang_id == syn) {
            return &E.lsp[i];
        }
    }

    int slot = -1;
    for (int i = 0; i < MAX_LSP_SERVERS; i++) {
        if (!E.lsp[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return NULL;

    const char *argv_c[] = {"clangd", "--background-index", "--log=error", NULL};
    const char *argv_py1[] = {"pyright-langserver", "--stdio", NULL};
    const char *argv_py2[] = {"pylsp", NULL};
    const char *argv_java[] = {"jdtls", NULL};
    const char *const *cmd_argv = NULL;
    const char *srv_name = NULL;

    if (syn == SYN_C) {
        if (!bin_in_path("clangd")) return NULL;
        cmd_argv = argv_c;
        srv_name = "clangd";
    } else if (syn == SYN_PYTHON) {
        if (bin_in_path("pyright-langserver")) {
            cmd_argv = argv_py1;
            srv_name = "pyright";
        } else if (bin_in_path("pylsp")) {
            cmd_argv = argv_py2;
            srv_name = "pylsp";
        } else {
            return NULL;
        }
    } else if (syn == SYN_JAVA) {
        if (!bin_in_path("jdtls")) return NULL;
        cmd_argv = argv_java;
        srv_name = "jdtls";
    }
    if (!cmd_argv) return NULL;

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0) return NULL;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return NULL;
    }

    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        execvp(cmd_argv[0], (char *const *)cmd_argv);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);

    LspServer *srv = &E.lsp[slot];
    memset(srv, 0, sizeof(LspServer));
    srv->active = 1;
    srv->initialized = 0;
    srv->lang_id = syn;
    snprintf(srv->name, sizeof(srv->name), "%s", srv_name);
    srv->pid = pid;
    srv->fd_in = in_pipe[1];
    srv->fd_out = out_pipe[0];
    srv->next_id = 1;

    /* Send LSP initialize */
    char root_uri[1200];
    build_uri(E.root_dir, root_uri, sizeof(root_uri));
    char *esc_uri = json_escape(root_uri);
    int req_id = srv->next_id++;
    srv->last_req_id = req_id;
    srv->last_req_kind = LSP_REQ_INIT;

    Abuf req = {NULL, 0, 0};
    ab_append_fmt(&req,
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"initialize\",\"params\":{"
        "\"processId\":%d,\"rootUri\":%s,\"capabilities\":{"
        "\"textDocument\":{\"completion\":{\"completionItem\":{\"snippetSupport\":false}},"
        "\"hover\":{\"contentFormat\":[\"plaintext\",\"markdown\"]},"
        "\"publishDiagnostics\":{}}}}}",
        req_id, (int)getpid(), esc_uri);
    lsp_send(srv, req.b);
    ab_free(&req);
    free(esc_uri);

    editor_set_status("LSP: starting %s...", srv_name);
    return srv;
}

static void send_did_open(LspServer *srv, Buffer *b) {
    if (!srv || !srv->initialized || !b || !b->filename || b->readonly) return;
    if (b->lsp_opened) return;

    char uri[1200];
    build_uri(b->filename, uri, sizeof(uri));
    char *esc_uri = json_escape(uri);
    char *esc_txt = buf_full_text_json(b);
    b->lsp_version = 1;
    b->lsp_opened = 1;

    Abuf msg = {NULL, 0, 0};
    ab_append_fmt(&msg,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":%s,\"languageId\":\"%s\",\"version\":%d,\"text\":%s}}}",
        esc_uri, lsp_lang_str(b->syntax_id), b->lsp_version, esc_txt);
    lsp_send(srv, msg.b);
    ab_free(&msg);
    free(esc_uri);
    free(esc_txt);
}

void lsp_init_all(void) {
    memset(E.lsp, 0, sizeof(E.lsp));
    for (int i = 0; i < MAX_LSP_SERVERS; i++) {
        E.lsp[i].fd_in = -1;
        E.lsp[i].fd_out = -1;
    }
}

void lsp_shutdown_all(void) {
    for (int i = 0; i < MAX_LSP_SERVERS; i++) {
        LspServer *srv = &E.lsp[i];
        if (!srv->active) continue;
        lsp_send(srv, "{\"jsonrpc\":\"2.0\",\"id\":9999,\"method\":\"shutdown\",\"params\":null}");
        lsp_send(srv, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}");
        if (srv->fd_in >= 0) close(srv->fd_in);
        if (srv->fd_out >= 0) close(srv->fd_out);
        if (srv->pid > 0) {
            kill(srv->pid, SIGTERM);
            waitpid(srv->pid, NULL, WNOHANG);
        }
        free(srv->rx_buf);
        memset(srv, 0, sizeof(LspServer));
        srv->fd_in = -1;
        srv->fd_out = -1;
    }
}

void lsp_on_buf_open(int buf_idx) {
    if (buf_idx < 0 || buf_idx >= E.num_buffers) return;
    Buffer *b = &E.buffers[buf_idx];
    if (!b->filename || b->readonly) return;
    LspServer *srv = find_or_start_server(b->syntax_id);
    if (srv && srv->initialized) {
        send_did_open(srv, b);
    }
}

void lsp_on_buf_change(int buf_idx) {
    if (buf_idx < 0 || buf_idx >= E.num_buffers) return;
    Buffer *b = &E.buffers[buf_idx];
    if (!b->filename || b->readonly) return;
    LspServer *srv = find_or_start_server(b->syntax_id);
    if (!srv || !srv->initialized) return;
    if (!b->lsp_opened) {
        send_did_open(srv, b);
        return;
    }

    char uri[1200];
    build_uri(b->filename, uri, sizeof(uri));
    char *esc_uri = json_escape(uri);
    char *esc_txt = buf_full_text_json(b);
    b->lsp_version++;

    Abuf msg = {NULL, 0, 0};
    ab_append_fmt(&msg,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":%s,\"version\":%d},"
        "\"contentChanges\":[{\"text\":%s}]}}",
        esc_uri, b->lsp_version, esc_txt);
    lsp_send(srv, msg.b);
    ab_free(&msg);
    free(esc_uri);
    free(esc_txt);
}

void lsp_on_buf_save(int buf_idx) {
    if (buf_idx < 0 || buf_idx >= E.num_buffers) return;
    Buffer *b = &E.buffers[buf_idx];
    if (!b->filename || b->readonly) return;
    LspServer *srv = find_or_start_server(b->syntax_id);
    if (!srv || !srv->initialized) return;

    char uri[1200];
    build_uri(b->filename, uri, sizeof(uri));
    char *esc_uri = json_escape(uri);
    Abuf msg = {NULL, 0, 0};
    ab_append_fmt(&msg,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
        "\"textDocument\":{\"uri\":%s}}}", esc_uri);
    lsp_send(srv, msg.b);
    ab_free(&msg);
    free(esc_uri);
}

static void send_pos_request(int buf_idx, const char *method, LspReqKind kind) {
    if (buf_idx < 0 || buf_idx >= E.num_buffers) return;
    Buffer *b = &E.buffers[buf_idx];
    if (!b->filename || b->readonly) return;
    LspServer *srv = find_or_start_server(b->syntax_id);
    if (!srv || !srv->initialized) {
        if (kind != LSP_REQ_COMPLETION) {
            editor_set_status("LSP server is initializing or unavailable");
        }
        return;
    }
    if (!b->lsp_opened) send_did_open(srv, b);

    char uri[1200];
    build_uri(b->filename, uri, sizeof(uri));
    char *esc_uri = json_escape(uri);
    int req_id = srv->next_id++;
    srv->last_req_id = req_id;
    srv->last_req_kind = kind;
    srv->req_buf_idx = buf_idx;

    Abuf msg = {NULL, 0, 0};
    ab_append_fmt(&msg,
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":{"
        "\"textDocument\":{\"uri\":%s},\"position\":{\"line\":%d,\"character\":%d}}}",
        req_id, method, esc_uri, b->cy, b->cx);
    lsp_send(srv, msg.b);
    ab_free(&msg);
    free(esc_uri);
}

void lsp_request_completion(int buf_idx) {
    send_pos_request(buf_idx, "textDocument/completion", LSP_REQ_COMPLETION);
}

void lsp_request_hover(int buf_idx) {
    send_pos_request(buf_idx, "textDocument/hover", LSP_REQ_HOVER);
}

void lsp_request_definition(int buf_idx) {
    send_pos_request(buf_idx, "textDocument/definition", LSP_REQ_DEFINITION);
}

void lsp_request_references(int buf_idx) {
    if (buf_idx < 0 || buf_idx >= E.num_buffers) return;
    Buffer *b = &E.buffers[buf_idx];
    if (!b->filename || b->readonly) return;
    LspServer *srv = find_or_start_server(b->syntax_id);
    if (!srv || !srv->initialized) {
        editor_set_status("LSP not ready yet");
        return;
    }
    char uri[1200];
    build_uri(b->filename, uri, sizeof(uri));
    char *esc_uri = json_escape(uri);
    int req_id = srv->next_id++;
    srv->last_req_id = req_id;
    srv->last_req_kind = LSP_REQ_REFERENCES;
    srv->req_buf_idx = buf_idx;

    Abuf msg = {NULL, 0, 0};
    ab_append_fmt(&msg,
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/references\",\"params\":{"
        "\"textDocument\":{\"uri\":%s},\"position\":{\"line\":%d,\"character\":%d},"
        "\"context\":{\"includeDeclaration\":true}}}",
        req_id, esc_uri, b->cy, b->cx);
    lsp_send(srv, msg.b);
    ab_free(&msg);
    free(esc_uri);
}

void lsp_request_rename(int buf_idx, const char *new_name) {
    if (buf_idx < 0 || buf_idx >= E.num_buffers || !new_name || !new_name[0]) return;
    Buffer *b = &E.buffers[buf_idx];
    if (!b->filename || b->readonly) return;
    LspServer *srv = find_or_start_server(b->syntax_id);
    if (!srv || !srv->initialized) {
        editor_set_status("LSP not ready yet");
        return;
    }

    char uri[1200];
    build_uri(b->filename, uri, sizeof(uri));
    char *esc_uri = json_escape(uri);
    char *esc_new = json_escape(new_name);
    int req_id = srv->next_id++;
    srv->last_req_id = req_id;
    srv->last_req_kind = LSP_REQ_RENAME;
    srv->req_buf_idx = buf_idx;

    Abuf msg = {NULL, 0, 0};
    ab_append_fmt(&msg,
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/rename\",\"params\":{"
        "\"textDocument\":{\"uri\":%s},\"position\":{\"line\":%d,\"character\":%d},\"newName\":%s}}",
        req_id, esc_uri, b->cy, b->cx, esc_new);
    lsp_send(srv, msg.b);
    ab_free(&msg);
    free(esc_uri);
    free(esc_new);
}

static void handle_diagnostics(JsonVal *params) {
    const char *uri = json_str(json_get(params, "uri"));
    JsonVal *diags = json_get(params, "diagnostics");
    if (!uri || !diags || diags->type != J_ARR) return;

    char path[1200];
    uri_to_path(uri, path, sizeof(path));

    for (int bi = 0; bi < E.num_buffers; bi++) {
        Buffer *b = &E.buffers[bi];
        if (!b->filename) continue;
        char b_uri[1200];
        build_uri(b->filename, b_uri, sizeof(b_uri));
        if (strcmp(b_uri, uri) != 0 && strcmp(b->filename, path) != 0) continue;

        for (int r = 0; r < b->num_lines; r++) {
            b->lines[r].diag_level = 0;
            b->lines[r].diag_msg[0] = '\0';
        }
        int err_cnt = 0;
        for (int i = 0; i < diags->count; i++) {
            JsonVal *d = diags->items[i];
            JsonVal *range = json_get(d, "range");
            JsonVal *start = json_get(range, "start");
            int line = (int)json_num(json_get(start, "line"), -1);
            int sev = (int)json_num(json_get(d, "severity"), 1);
            const char *msg = json_str(json_get(d, "message"));
            if (line >= 0 && line < b->num_lines && msg) {
                int lvl = (sev == 1) ? 2 : 1;
                if (lvl > b->lines[line].diag_level) {
                    b->lines[line].diag_level = lvl;
                    snprintf(b->lines[line].diag_msg, sizeof(b->lines[line].diag_msg), "%s", msg);
                    for (char *p = b->lines[line].diag_msg; *p; p++) {
                        if (*p == '\n' || *p == '\r') *p = ' ';
                    }
                }
                if (sev == 1) err_cnt++;
            }
        }
        if (err_cnt > 0 && bi == E.cur_buf) {
            editor_set_status("LSP: %d error(s) — press ]d to jump", err_cnt);
        }
    }
}

static void handle_lsp_message(LspServer *srv, const char *json_str_body) {
    JsonVal *root = json_parse(json_str_body);
    if (!root) return;

    const char *method = json_str(json_get(root, "method"));
    if (method) {
        if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
            handle_diagnostics(json_get(root, "params"));
        }
        json_free(root);
        return;
    }

    int id = (int)json_num(json_get(root, "id"), -1);
    JsonVal *result = json_get(root, "result");
    if (id != srv->last_req_id || !result) {
        json_free(root);
        return;
    }

    LspReqKind kind = srv->last_req_kind;
    srv->last_req_kind = LSP_REQ_NONE;

    if (kind == LSP_REQ_INIT) {
        srv->initialized = 1;
        lsp_send(srv, "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
        for (int i = 0; i < E.num_buffers; i++) {
            if (E.buffers[i].syntax_id == srv->lang_id && E.buffers[i].filename) {
                send_did_open(srv, &E.buffers[i]);
            }
        }
        editor_set_status("LSP ready: %s", srv->name);
    } else if (kind == LSP_REQ_COMPLETION) {
        JsonVal *items = (result->type == J_ARR) ? result : json_get(result, "items");
        if (items && items->type == J_ARR) {
            E.lsp_ac_count = 0;
            for (int i = 0; i < items->count && E.lsp_ac_count < MAX_AC_ITEMS; i++) {
                const char *lbl = json_str(json_get(items->items[i], "label"));
                if (!lbl || !lbl[0]) continue;
                snprintf(E.lsp_ac_items[E.lsp_ac_count], sizeof(E.lsp_ac_items[0]), "%s", lbl);
                snprintf(E.lsp_ac_kinds[E.lsp_ac_count], sizeof(E.lsp_ac_kinds[0]), "lsp");
                E.lsp_ac_count++;
            }
            if (E.mode == MODE_INSERT) {
                ac_refresh(&E.buffers[E.cur_buf], 0);
            }
        }
    } else if (kind == LSP_REQ_HOVER) {
        JsonVal *contents = json_get(result, "contents");
        const char *txt = json_str(contents);
        if (!txt && contents && contents->type == J_OBJ) {
            txt = json_str(json_get(contents, "value"));
        }
        if (txt && txt[0]) {
            E.hover_count = 0;
            const char *p = txt;
            while (*p && E.hover_count < 16) {
                const char *nl = strchr(p, '\n');
                int len = nl ? (int)(nl - p) : (int)strlen(p);
                if (len > 120) len = 120;
                memcpy(E.hover_lines[E.hover_count], p, (size_t)len);
                E.hover_lines[E.hover_count][len] = '\0';
                E.hover_count++;
                if (!nl) break;
                p = nl + 1;
            }
            E.hover_visible = (E.hover_count > 0);
        } else {
            editor_set_status("LSP: no hover info");
        }
    } else if (kind == LSP_REQ_DEFINITION) {
        JsonVal *loc = (result->type == J_ARR && result->count > 0) ? result->items[0] : result;
        const char *uri = json_str(json_get(loc, "uri"));
        if (!uri) uri = json_str(json_get(loc, "targetUri"));
        JsonVal *range = json_get(loc, "range");
        if (!range) range = json_get(loc, "targetSelectionRange");
        JsonVal *start = json_get(range, "start");
        int line = (int)json_num(json_get(start, "line"), 0);
        int ch = (int)json_num(json_get(start, "character"), 0);
        if (uri) {
            char path[1200];
            uri_to_path(uri, path, sizeof(path));
            if (buf_open(path) >= 0) {
                Buffer *nb = &E.buffers[E.cur_buf];
                if (line >= nb->num_lines) line = nb->num_lines - 1;
                if (line < 0) line = 0;
                nb->cy = line;
                nb->cx = ch < nb->lines[line].size ? ch : nb->lines[line].size;
                E.focus = FOCUS_EDITOR;
            }
        } else {
            editor_set_status("LSP: definition not found");
        }
    } else if (kind == LSP_REQ_REFERENCES && result->type == J_ARR) {
        Abuf out = {NULL, 0, 0};
        ab_append_str(&out, "# LSP References (press Enter on a line to jump)\n\n");
        for (int i = 0; i < result->count; i++) {
            const char *uri = json_str(json_get(result->items[i], "uri"));
            JsonVal *start = json_get(json_get(result->items[i], "range"), "start");
            int line = (int)json_num(json_get(start, "line"), 0) + 1;
            int ch = (int)json_num(json_get(start, "character"), 0) + 1;
            if (uri) {
                char path[1200];
                uri_to_path(uri, path, sizeof(path));
                ab_append_fmt(&out, "%s:%d: col %d\n", path, line, ch);
            }
        }
        buf_open_scratch("[Grep]", out.b ? out.b : "No references found.\n", 0);
        ab_free(&out);
    } else if (kind == LSP_REQ_RENAME && result->type == J_OBJ) {
        JsonVal *changes = json_get(result, "changes");
        if (changes && changes->type == J_OBJ) {
            int applied = 0;
            for (int f = 0; f < changes->count; f++) {
                char path[1200];
                uri_to_path(changes->keys[f], path, sizeof(path));
                JsonVal *edits = changes->items[f];
                if (buf_open(path) >= 0 && edits && edits->type == J_ARR) {
                    Buffer *tb = &E.buffers[E.cur_buf];
                    /* Apply in reverse order so coordinates stay valid */
                    for (int e = edits->count - 1; e >= 0; e--) {
                        JsonVal *ed = edits->items[e];
                        JsonVal *st = json_get(json_get(ed, "range"), "start");
                        JsonVal *en = json_get(json_get(ed, "range"), "end");
                        const char *nt = json_str(json_get(ed, "newText"));
                        int sr = (int)json_num(json_get(st, "line"), -1);
                        int sc = (int)json_num(json_get(st, "character"), 0);
                        int ec = (int)json_num(json_get(en, "character"), 0);
                        if (sr >= 0 && sr < tb->num_lines && nt) {
                            if (ec > sc) buf_raw_delete_at(tb, sr, sc, ec - sc);
                            buf_raw_insert_at(tb, sr, sc, nt, (int)strlen(nt));
                            applied++;
                        }
                    }
                }
            }
            editor_set_status("LSP Rename: applied %d edit(s)", applied);
        }
    }

    json_free(root);
}

int lsp_poll_events(void) {
    int updated = 0;
    for (int i = 0; i < MAX_LSP_SERVERS; i++) {
        LspServer *srv = &E.lsp[i];
        if (!srv->active || srv->fd_out < 0) continue;

        char tmp[4096];
        ssize_t n;
        while ((n = read(srv->fd_out, tmp, sizeof(tmp))) > 0) {
            if (srv->rx_len + (int)n + 1 > srv->rx_cap) {
                int ncap = srv->rx_cap ? srv->rx_cap * 2 : 16384;
                while (ncap < srv->rx_len + (int)n + 1) ncap *= 2;
                srv->rx_buf = realloc(srv->rx_buf, (size_t)ncap);
                srv->rx_cap = ncap;
            }
            memcpy(srv->rx_buf + srv->rx_len, tmp, (size_t)n);
            srv->rx_len += (int)n;
            srv->rx_buf[srv->rx_len] = '\0';
        }
        if (n == 0) {
            /* Server exited */
            close(srv->fd_out);
            srv->fd_out = -1;
            srv->active = 0;
            continue;
        }

        /* Drain complete Content-Length frames */
        while (srv->rx_buf && srv->rx_len > 0) {
            char *hdr_end = strstr(srv->rx_buf, "\r\n\r\n");
            if (!hdr_end) break;

            int content_len = -1;
            char *cl = strstr(srv->rx_buf, "Content-Length:");
            if (cl && cl < hdr_end) content_len = atoi(cl + 15);
            if (content_len < 0) {
                srv->rx_len = 0;
                break;
            }

            int hdr_bytes = (int)(hdr_end - srv->rx_buf) + 4;
            if (srv->rx_len < hdr_bytes + content_len) break;

            char *body = malloc((size_t)content_len + 1);
            memcpy(body, srv->rx_buf + hdr_bytes, (size_t)content_len);
            body[content_len] = '\0';

            int total_consumed = hdr_bytes + content_len;
            memmove(srv->rx_buf, srv->rx_buf + total_consumed,
                    (size_t)(srv->rx_len - total_consumed + 1));
            srv->rx_len -= total_consumed;

            handle_lsp_message(srv, body);
            free(body);
            updated = 1;
        }
    }
    return updated;
}

void lsp_status_report(void) {
    Abuf out = {NULL, 0, 0};
    ab_append_str(&out, "==============================================================\n");
    ab_append_str(&out, "               catingiran LSP Status (:lsp)\n");
    ab_append_str(&out, "==============================================================\n");
    ab_append_fmt(&out, "LSP Enabled : %s\n\n", E.cfg.lsp_enabled ? "true" : "false");

    int count = 0;
    for (int i = 0; i < MAX_LSP_SERVERS; i++) {
        LspServer *srv = &E.lsp[i];
        if (!srv->active) continue;
        ab_append_fmt(&out, "  %-18s  lang=%-8s  pid=%d  state=%s\n",
                      srv->name, syntax_name(srv->lang_id), (int)srv->pid,
                      srv->initialized ? "READY" : "INITIALIZING");
        count++;
    }
    if (count == 0) {
        ab_append_str(&out, "No active LSP servers yet (open a .c, .py, or .java file).\n\n");
    }
    ab_append_str(&out, "\nLSP Shortcuts:\n");
    ab_append_str(&out, "  gd          Go to definition\n");
    ab_append_str(&out, "  gr          Find all references\n");
    ab_append_str(&out, "  K           Show hover documentation\n");
    ab_append_str(&out, "  Space+rn    Rename symbol across project\n");
    ab_append_str(&out, "  ]d / [d     Next / Previous diagnostic\n");

    buf_open_scratch("[LSP]", out.b, 0);
    ab_free(&out);
}
