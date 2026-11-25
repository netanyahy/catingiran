#ifndef CATINGIRAN_STATE_H
#define CATINGIRAN_STATE_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#define CTI_VERSION "1.0.0"
#define CTRL_KEY(k) ((k) & 0x1f)
#define MAX_BUFFERS 32
#define MAX_PLUGINS 32
#define MAX_PLUGIN_CMDS 64
#define MAX_PLUGIN_WORDS 1024
#define MAX_OUTPUT_LINES 200
#define MAX_AC_ITEMS 14
#define MAX_LSP_SERVERS 4
#define MAX_MACRO_KEYS 512

typedef enum {
    KEY_NULL = 0,
    KEY_TAB = 9,
    KEY_ENTER = 13,
    KEY_ESC = 27,
    KEY_BACKSPACE = 127,
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DEL,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_SHIFT_TAB,
    KEY_F2,
    KEY_F5,
    KEY_CTRL_SPACE,
    KEY_MOUSE
} EditorKey;

typedef enum {
    MODE_NORMAL = 0,
    MODE_INSERT,
    MODE_VISUAL,
    MODE_COMMAND,
    MODE_SEARCH
} EditorMode;

typedef enum {
    FOCUS_EDITOR = 0,
    FOCUS_SIDEBAR
} FocusPane;

typedef enum {
    SPLIT_NONE = 0,
    SPLIT_VERT,
    SPLIT_HORIZ
} SplitMode;

typedef enum {
    SYN_NONE = 0,
    SYN_C,
    SYN_PYTHON,
    SYN_JAVA,
    SYN_JS,
    SYN_SHELL,
    SYN_CONF
} SyntaxLang;

typedef enum {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_PREPROC,
    HL_FUNC,
    HL_MATCH
} HlType;

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char trans; /* 1 = transparent terminal background (\x1b[49m) */
} Color;

typedef struct {
    char name[32];
    Color bg;
    Color fg;
    Color gutter_fg;
    Color gutter_bg;
    Color cursorline_bg;
    Color selection_bg;
    Color sidebar_bg;
    Color sidebar_fg;
    Color sidebar_sel_bg;
    Color sidebar_sel_fg;
    Color sidebar_dir_fg;
    Color border_fg;
    Color tab_bg;
    Color tab_fg;
    Color tab_active_bg;
    Color tab_active_fg;
    Color status_bg;
    Color status_fg;
    Color status_mode_bg;
    Color status_mode_fg;
    Color hl_comment;
    Color hl_kw1;
    Color hl_kw2;
    Color hl_string;
    Color hl_number;
    Color hl_preproc;
    Color hl_func;
    Color hl_match;
} Theme;

typedef struct {
    char spec[128];
    char name[64];
    char url[256];
    int installed;
    int is_builtin;
} PluginSpec;

typedef struct {
    char name[48];
    char shell_cmd[256];
    char plugin_name[64];
} PluginCommand;

typedef struct {
    char word[64];
    char kind[16];
    int lang_id; /* 0 = all */
} PluginWord;

typedef struct {
    char theme[32];
    int show_tabs;
    int statusline;
    int line_numbers;
    int relative_numbers;
    int cursorline;
    int colorcolumn;
    int nerd_icons;
    int tab_size;
    int expand_tab;
    int auto_indent;
    int auto_pairs;
    int highlight_current_word;
    int trailing_whitespace;
    int trim_on_save;
    int scrolloff;
    int mouse_enabled;
    int russian_keymap;
    int ignore_case;
    int smart_case;
    int hl_search;
    int undo_levels;
    int persistent_undo;
    int confirm_quit;
    int sidebar_open_on_start;
    int sidebar_width;
    int sidebar_right; /* 0=left, 1=right */
    int show_hidden;
    int output_height;
    int auto_complete;
    int autocomplete_min_chars;
    int auto_lint;
    int lsp_enabled;
    int lsp_auto_hover;
    int git_branch;
    PluginSpec plugins[MAX_PLUGINS];
    int num_plugins;
} Config;

typedef struct {
    char *chars;
    int size;
    int cap;
    unsigned char *hl;
    int hl_cap;
    int hl_open_comment;
    int bookmarked;
    int diag_level; /* 0=none, 1=warn, 2=error */
    char diag_msg[128];
} Line;

typedef enum {
    UNDO_INS_STR = 0,
    UNDO_DEL_STR,
    UNDO_SPLIT_LINE,
    UNDO_JOIN_LINE,
    UNDO_DEL_LINE,
    UNDO_INS_LINE
} UndoKind;

typedef struct {
    UndoKind kind;
    int row;
    int col;
    char *text;
    int len;
} UndoAction;

typedef struct {
    char *filename;
    Line *lines;
    int num_lines;
    int cap_lines;
    int cx;
    int cy;
    int rowoff;
    int coloff;
    int vis_sx;
    int vis_sy;
    int dirty;
    int syntax_id;
    int syntax_manual;
    int readonly;
    int lsp_version;
    int lsp_opened;
    UndoAction *undo;
    int undo_len;
    int undo_cap;
    UndoAction *redo;
    int redo_len;
    int redo_cap;
    int undo_recording;
} Buffer;

typedef struct {
    char *name;
    char *path;
    int is_dir;
    int expanded;
    int depth;
} TreeNode;

typedef struct {
    int btn;
    int x;
    int y;
    int pressed;
} MouseEvent;

typedef struct {
    char *b;
    int len;
    int cap;
} Abuf;

/* Minimal JSON value tree for LSP JSON-RPC 2.0 */
typedef enum {
    J_NULL = 0,
    J_BOOL,
    J_NUM,
    J_STR,
    J_ARR,
    J_OBJ
} JsonType;

typedef struct JsonVal JsonVal;
struct JsonVal {
    JsonType type;
    int bool_val;
    double num_val;
    char *str_val;
    JsonVal **items;
    char **keys;
    int count;
};

typedef enum {
    LSP_REQ_NONE = 0,
    LSP_REQ_INIT,
    LSP_REQ_COMPLETION,
    LSP_REQ_HOVER,
    LSP_REQ_DEFINITION,
    LSP_REQ_REFERENCES,
    LSP_REQ_RENAME
} LspReqKind;

typedef struct {
    int active;
    int initialized;
    int lang_id; /* SYN_C, SYN_PYTHON, SYN_JAVA */
    char name[32];
    pid_t pid;
    int fd_in;  /* write to server */
    int fd_out; /* read from server */
    char *rx_buf;
    int rx_len;
    int rx_cap;
    int next_id;
    int last_req_id;
    LspReqKind last_req_kind;
    int req_buf_idx;
} LspServer;

typedef struct {
    int key;
    int ulen;
    char ubuf[5];
} MacroKey;

typedef struct {
    int screen_rows;
    int screen_cols;
    Buffer buffers[MAX_BUFFERS];
    int num_buffers;
    int cur_buf;
    SplitMode split_mode;
    int split_buf;
    int active_pane;
    TreeNode *tree;
    int tree_len;
    int tree_cap;
    int tree_sel;
    int tree_scroll;
    char root_dir[1024];
    char git_branch[64];
    int sidebar_visible;
    int output_visible;
    char output_lines[MAX_OUTPUT_LINES][256];
    int output_len;
    char output_title[64];
    FocusPane focus;
    EditorMode mode;
    char cmd_buf[512];
    int cmd_len;
    int cmd_pos;
    char cmd_history[32][512];
    int cmd_hist_len;
    int cmd_hist_idx;
    char search_buf[256];
    int search_len;
    int search_match_row;
    int search_match_col;
    char status_msg[256];
    time_t status_msg_time;
    char *clipboard;
    /* Vim grammar state */
    int count_prefix;
    char pending_op; /* 'd', 'c', 'y', 'g', '>', '<', ']', '[' */
    char pending_obj_mod; /* 'i' or 'a' */
    int pending_leader;
    int pending_f;
    int pending_r;
    /* Macros */
    int macro_recording;
    char macro_rec_reg;
    MacroKey macros[26][MAX_MACRO_KEYS];
    int macro_lens[26];
    int macro_playing;
    /* Mouse & Config */
    MouseEvent last_mouse;
    Config cfg;
    Theme theme;
    PluginCommand plugin_cmds[MAX_PLUGIN_CMDS];
    int num_plugin_cmds;
    PluginWord plugin_words[MAX_PLUGIN_WORDS];
    int num_plugin_words;
    /* Autocomplete popup */
    int ac_visible;
    char ac_items[MAX_AC_ITEMS][64];
    char ac_kinds[MAX_AC_ITEMS][16];
    int ac_count;
    int ac_sel;
    int ac_prefix_len;
    /* Floating hover / info popup */
    int hover_visible;
    char hover_lines[16][128];
    int hover_count;
    /* LSP servers & completions cache */
    LspServer lsp[MAX_LSP_SERVERS];
    char lsp_ac_items[MAX_AC_ITEMS][64];
    char lsp_ac_kinds[MAX_AC_ITEMS][16];
    int lsp_ac_count;
    int running;
} EditorState;

extern EditorState E;

/* terminal.c */
void term_enable_raw(void);
void term_disable_raw(void);
void term_update_size(void);
int term_wait_input(int timeout_ms);
int term_read_key(int *utf8_bytes_out, char utf8_buf[5]);
void ab_append(Abuf *ab, const char *s, int len);
void ab_append_str(Abuf *ab, const char *s);
void ab_append_fmt(Abuf *ab, const char *fmt, ...);
void ab_append_fg(Abuf *ab, Color c);
void ab_append_bg(Abuf *ab, Color c);
void ab_free(Abuf *ab);

/* config.c */
void config_init_defaults(Config *cfg);
void config_load(Config *cfg);
int theme_apply(const char *name, Theme *out);
const char *theme_list_names(void);
void config_get_user_path(char *buf, size_t sz);

/* buffer.c */
int utf8_char_len(unsigned char c);
int utf8_prev_idx(const char *s, int pos);
int utf8_next_idx(const char *s, int size, int pos);
int utf8_str_width(const char *s, int bytes);
int buf_cx_to_rx(Buffer *b, int cy, int cx);
void buf_init_empty(Buffer *b);
void buf_free(Buffer *b);
int buf_open(const char *filename);
int buf_open_scratch(const char *title, const char *content, int syntax_id);
int buf_save(Buffer *b, const char *new_name);
void buf_close(int idx, int force);
void buf_insert_bytes(Buffer *b, const char *s, int len);
void buf_insert_newline(Buffer *b);
void buf_backspace(Buffer *b);
void buf_delete_at_cursor(Buffer *b);
void buf_delete_line(Buffer *b, int row);
void buf_delete_range(Buffer *b, int sr, int sc, int er, int ec, int copy_to_clip);
void buf_indent_line(Buffer *b, int row, int delta);
void buf_undo(Buffer *b);
void buf_redo(Buffer *b);
void buf_find(Buffer *b, const char *query, int dir);
int buf_replace_all(Buffer *b, const char *old_s, const char *new_s, int cur_line_only);
void buf_raw_insert_at(Buffer *b, int row, int col, const char *s, int len);
void buf_raw_delete_at(Buffer *b, int row, int col, int len);

/* syntax.c */
int syntax_detect(const char *filename);
int syntax_detect_from_content(Buffer *b);
int syntax_id_from_name(const char *name);
const char *syntax_name(int syntax_id);
void syntax_update_line(Buffer *b, int row);
void syntax_update_all(Buffer *b);
Color syntax_color(HlType hl);

/* tree.c */
void tree_init(const char *root_path);
void tree_free(void);
void tree_refresh(void);
void tree_toggle_selected(void);
void tree_collapse_or_parent(void);
void tree_create_prompt(void);
void tree_delete_selected(void);
void tree_rename_selected(void);

/* ui.c */
void ui_render(void);
void editor_set_status(const char *fmt, ...);
void editor_prompt_input(const char *prompt, char *out, int out_sz);

/* commands.c */
void cmd_execute(const char *cmd);
void cmd_autocomplete(char *buf, int *len, int *pos, int max_sz);

/* plugins.c */
void plugins_get_dir(char *buf, size_t sz);
void plugins_load_all(void);
void plugins_install(void);
void plugins_update(void);
void plugins_clean(void);
void plugins_status(void);
void plugins_list_catalog(void);
void plugins_add(const char *spec);
void plugins_create(const char *name);
int plugins_try_command(const char *cmd);

/* tools.c */
void tools_clear_output(const char *title);
void tools_append_output(const char *line);
void tools_run_current(void);
void tools_toggle_output(void);
void tools_spawn_shell(void);
void tools_grep(const char *query);
void tools_fuzzy_find(void);
void tools_git_status(void);
void tools_git_diff(void);
void tools_refresh_git_branch(void);
void tools_project_stats(void);
void tools_toggle_comment(Buffer *b, int start_row, int end_row);
void tools_duplicate_line(Buffer *b);
void tools_move_lines(Buffer *b, int start_row, int end_row, int dir);
void tools_toggle_bookmark(Buffer *b, int row);
void tools_next_bookmark(Buffer *b);
void tools_jump_grep_line(Buffer *b);
void tools_lint_buffer(Buffer *b);
void tools_next_diag(Buffer *b, int dir);
void tools_format_buffer(Buffer *b);

/* autocomplete.c */
void ac_refresh(Buffer *b, int force);
void ac_dismiss(void);
void ac_move(int delta);
void ac_accept(Buffer *b);

/* json.c */
JsonVal *json_parse(const char *s);
void json_free(JsonVal *v);
JsonVal *json_get(JsonVal *obj, const char *key);
const char *json_str(JsonVal *v);
double json_num(JsonVal *v, double def);
char *json_escape(const char *s);

/* lsp.c */
void lsp_init_all(void);
void lsp_shutdown_all(void);
void lsp_on_buf_open(int buf_idx);
void lsp_on_buf_change(int buf_idx);
void lsp_on_buf_save(int buf_idx);
void lsp_request_completion(int buf_idx);
void lsp_request_hover(int buf_idx);
void lsp_request_definition(int buf_idx);
void lsp_request_references(int buf_idx);
void lsp_request_rename(int buf_idx, const char *new_name);
int lsp_poll_events(void);
void lsp_status_report(void);

#endif /* CATINGIRAN_STATE_H */
