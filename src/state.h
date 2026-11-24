#ifndef CATINGIRAN_STATE_H
#define CATINGIRAN_STATE_H

#include <stddef.h>
#include <time.h>

#define CTI_VERSION "0.2.0"
#define CTRL_KEY(k) ((k) & 0x1f)
#define MAX_BUFFERS 32
#define MAX_PLUGINS 32
#define MAX_PLUGIN_CMDS 64
#define MAX_OUTPUT_LINES 200

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
} PluginSpec;

typedef struct {
    char name[48];
    char shell_cmd[256];
    char plugin_name[64];
} PluginCommand;

typedef struct {
    char theme[32];
    int tab_size;
    int expand_tab;
    int line_numbers;
    int cursorline;
    int auto_indent;
    int auto_pairs;
    int mouse_enabled;
    int sidebar_open_on_start;
    int sidebar_width;
    int show_hidden;
    int nerd_icons;
    int russian_keymap;
    int show_tabs;
    int output_height;
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
    int readonly;
    UndoAction *undo;
    int undo_len;
    int undo_cap;
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

typedef struct {
    int screen_rows;
    int screen_cols;
    Buffer buffers[MAX_BUFFERS];
    int num_buffers;
    int cur_buf;
    SplitMode split_mode;
    int split_buf;
    int active_pane; /* 0 = primary, 1 = secondary */
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
    int pending_g;
    int pending_d;
    int pending_y;
    int pending_c;
    int pending_leader;
    int pending_f;
    int quit_warn;
    MouseEvent last_mouse;
    Config cfg;
    Theme theme;
    PluginCommand plugin_cmds[MAX_PLUGIN_CMDS];
    int num_plugin_cmds;
    int running;
} EditorState;

extern EditorState E;

/* terminal.c */
void term_enable_raw(void);
void term_disable_raw(void);
void term_update_size(void);
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
void buf_indent_line(Buffer *b, int row, int delta);
void buf_undo(Buffer *b);
void buf_find(Buffer *b, const char *query, int dir);
void buf_raw_insert_at(Buffer *b, int row, int col, const char *s, int len);
void buf_raw_delete_at(Buffer *b, int row, int col, int len);

/* syntax.c */
int syntax_detect(const char *filename);
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
void plugins_add(const char *spec);
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

#endif /* CATINGIRAN_STATE_H */
