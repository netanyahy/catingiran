#include "state.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum {
    SYN_NONE = 0,
    SYN_C,
    SYN_PYTHON,
    SYN_JS,
    SYN_SHELL,
    SYN_CONF
};

static const char *C_KW1[] = {
    "if", "else", "for", "while", "do", "switch", "case", "default",
    "break", "continue", "return", "goto", "sizeof", "struct", "union",
    "enum", "typedef", "static", "extern", "const", "volatile", "inline",
    "NULL", "true", "false", NULL
};

static const char *C_KW2[] = {
    "void", "int", "char", "short", "long", "float", "double", "signed",
    "unsigned", "size_t", "ssize_t", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", "int32_t", "int64_t", "bool", "FILE", NULL
};

static const char *PY_KW1[] = {
    "def", "class", "if", "elif", "else", "for", "while", "break",
    "continue", "return", "import", "from", "as", "with", "try",
    "except", "finally", "raise", "pass", "yield", "lambda", "and",
    "or", "not", "in", "is", "async", "await", NULL
};

static const char *PY_KW2[] = {
    "None", "True", "False", "self", "cls", "int", "str", "list",
    "dict", "set", "tuple", "bool", "float", "print", "len", "range", NULL
};

static const char *JS_KW1[] = {
    "function", "const", "let", "var", "if", "else", "for", "while",
    "do", "switch", "case", "default", "break", "continue", "return",
    "import", "export", "from", "class", "extends", "new", "try",
    "catch", "finally", "throw", "async", "await", "typeof", NULL
};

static const char *JS_KW2[] = {
    "null", "undefined", "true", "false", "this", "super", "console",
    "window", "document", "Promise", "Array", "Object", "String", "Number", NULL
};

static const char *SH_KW1[] = {
    "if", "then", "elif", "else", "fi", "for", "in", "do", "done",
    "while", "until", "case", "esac", "function", "return", "exit",
    "export", "local", "readonly", "shift", "set", "unset", NULL
};

static const char *SH_KW2[] = {
    "echo", "printf", "grep", "awk", "sed", "cat", "mkdir", "rm",
    "cp", "mv", "chmod", "chown", "install", "true", "false", NULL
};

static int has_ext(const char *fn, const char *ext) {
    size_t fl = strlen(fn);
    size_t el = strlen(ext);
    return fl >= el && strcmp(fn + fl - el, ext) == 0;
}

int syntax_detect(const char *filename) {
    if (!filename) return SYN_NONE;
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    if (strcmp(base, "Makefile") == 0 || strcmp(base, "makefile") == 0)
        return SYN_SHELL;

    if (has_ext(base, ".c") || has_ext(base, ".h") ||
        has_ext(base, ".cpp") || has_ext(base, ".hpp") || has_ext(base, ".cc"))
        return SYN_C;
    if (has_ext(base, ".py"))
        return SYN_PYTHON;
    if (has_ext(base, ".js") || has_ext(base, ".ts") ||
        has_ext(base, ".jsx") || has_ext(base, ".tsx") || has_ext(base, ".json"))
        return SYN_JS;
    if (has_ext(base, ".sh") || has_ext(base, ".bash") ||
        has_ext(base, ".mksh") || has_ext(base, ".zsh"))
        return SYN_SHELL;
    if (has_ext(base, ".conf") || has_ext(base, ".ini") ||
        has_ext(base, ".cfg") || has_ext(base, ".md") || has_ext(base, ".yuck"))
        return SYN_CONF;
    return SYN_NONE;
}

static int is_sep(int c) {
    return isspace((unsigned char)c) || c == '\0' ||
           strchr(",.()+-/*=~%<>[];:{}!&|^", c) != NULL;
}

static int match_kw(const char *s, int rem, const char **kws) {
    if (!kws) return 0;
    for (int i = 0; kws[i]; i++) {
        int kl = (int)strlen(kws[i]);
        if (kl <= rem && strncmp(s, kws[i], (size_t)kl) == 0 && is_sep(s[kl])) {
            return kl;
        }
    }
    return 0;
}

void syntax_update_line(Buffer *b, int row) {
    if (row < 0 || row >= b->num_lines) return;
    Line *ln = &b->lines[row];

    if (ln->hl_cap < ln->size + 1) {
        int ncap = ln->size + 32;
        unsigned char *nh = realloc(ln->hl, (size_t)ncap);
        if (!nh) return;
        ln->hl = nh;
        ln->hl_cap = ncap;
    }
    memset(ln->hl, HL_NORMAL, (size_t)ln->size);

    if (b->syntax_id == SYN_NONE) return;

    const char **kw1 = NULL, **kw2 = NULL;
    const char *scs = NULL;
    const char *mcs = NULL, *mce = NULL;
    int has_preproc = 0;

    switch (b->syntax_id) {
        case SYN_C:
            kw1 = C_KW1; kw2 = C_KW2;
            scs = "//"; mcs = "/*"; mce = "*/"; has_preproc = 1;
            break;
        case SYN_PYTHON:
            kw1 = PY_KW1; kw2 = PY_KW2;
            scs = "#";
            break;
        case SYN_JS:
            kw1 = JS_KW1; kw2 = JS_KW2;
            scs = "//"; mcs = "/*"; mce = "*/";
            break;
        case SYN_SHELL:
            kw1 = SH_KW1; kw2 = SH_KW2;
            scs = "#";
            break;
        case SYN_CONF:
            scs = "#";
            break;
    }

    int prev_sep = 1;
    int in_str = 0;
    int in_comment = (row > 0) ? b->lines[row - 1].hl_open_comment : 0;

    int i = 0;
    while (i < ln->size) {
        char c = ln->chars[i];
        unsigned char prev_hl = (i > 0) ? ln->hl[i - 1] : HL_NORMAL;

        if (mcs && mce && !in_str) {
            if (in_comment) {
                ln->hl[i] = HL_COMMENT;
                if (strncmp(&ln->chars[i], mce, strlen(mce)) == 0) {
                    memset(&ln->hl[i], HL_COMMENT, strlen(mce));
                    i += (int)strlen(mce);
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                }
                i++;
                continue;
            } else if (strncmp(&ln->chars[i], mcs, strlen(mcs)) == 0) {
                memset(&ln->hl[i], HL_COMMENT, strlen(mcs));
                i += (int)strlen(mcs);
                in_comment = 1;
                continue;
            }
        }

        if (scs && !in_str && strncmp(&ln->chars[i], scs, strlen(scs)) == 0) {
            memset(&ln->hl[i], HL_COMMENT, (size_t)(ln->size - i));
            break;
        }

        if (has_preproc && prev_sep && c == '#' && !in_str) {
            memset(&ln->hl[i], HL_PREPROC, (size_t)(ln->size - i));
            break;
        }

        if (in_str) {
            ln->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < ln->size) {
                ln->hl[i + 1] = HL_STRING;
                i += 2;
                continue;
            }
            if (c == in_str) in_str = 0;
            i++;
            prev_sep = 1;
            continue;
        } else if (c == '"' || c == '\'') {
            in_str = c;
            ln->hl[i] = HL_STRING;
            i++;
            continue;
        }

        if ((isdigit((unsigned char)c) && (prev_sep || prev_hl == HL_NUMBER)) ||
            (c == '.' && prev_hl == HL_NUMBER)) {
            ln->hl[i] = HL_NUMBER;
            i++;
            prev_sep = 0;
            continue;
        }

        if (prev_sep) {
            int rem = ln->size - i;
            int kl = match_kw(&ln->chars[i], rem, kw1);
            if (kl > 0) {
                memset(&ln->hl[i], HL_KEYWORD1, (size_t)kl);
                i += kl;
                prev_sep = 0;
                continue;
            }
            kl = match_kw(&ln->chars[i], rem, kw2);
            if (kl > 0) {
                memset(&ln->hl[i], HL_KEYWORD2, (size_t)kl);
                i += kl;
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_sep(c);
        i++;
    }

    int changed = (ln->hl_open_comment != in_comment);
    ln->hl_open_comment = in_comment;
    if (changed && row + 1 < b->num_lines) {
        syntax_update_line(b, row + 1);
    }
}

void syntax_update_all(Buffer *b) {
    for (int i = 0; i < b->num_lines; i++) {
        syntax_update_line(b, i);
    }
}

Color syntax_color(HlType hl) {
    switch (hl) {
        case HL_COMMENT:  return E.theme.hl_comment;
        case HL_KEYWORD1: return E.theme.hl_kw1;
        case HL_KEYWORD2: return E.theme.hl_kw2;
        case HL_STRING:   return E.theme.hl_string;
        case HL_NUMBER:   return E.theme.hl_number;
        case HL_PREPROC:  return E.theme.hl_preproc;
        case HL_MATCH:    return E.theme.hl_match;
        case HL_NORMAL:
        default:          return E.theme.fg;
    }
}
