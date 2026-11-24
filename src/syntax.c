#include "state.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum {
    SYN_NONE = 0,
    SYN_C,
    SYN_PYTHON,
    SYN_JAVA,
    SYN_JS,
    SYN_SHELL,
    SYN_CONF
};

/* Multi-line state stored in Line.hl_open_comment */
enum {
    ML_NONE = 0,
    ML_BLOCK_COMMENT = 1,
    ML_TRIPLE_DQUOTE = 2,
    ML_TRIPLE_SQUOTE = 3
};

static const char *C_KW1[] = {
    "if", "else", "for", "while", "do", "switch", "case", "default",
    "break", "continue", "return", "goto", "sizeof", "struct", "union",
    "enum", "typedef", "static", "extern", "const", "volatile", "inline",
    "register", "restrict", "auto", "NULL", "true", "false", NULL
};

static const char *C_KW2[] = {
    "void", "int", "char", "short", "long", "float", "double", "signed",
    "unsigned", "size_t", "ssize_t", "ptrdiff_t", "uint8_t", "uint16_t",
    "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "bool", "FILE", "DIR", "time_t", NULL
};

static const char *PY_KW1[] = {
    "def", "class", "if", "elif", "else", "for", "while", "break",
    "continue", "return", "import", "from", "as", "with", "try",
    "except", "finally", "raise", "pass", "yield", "lambda", "and",
    "or", "not", "in", "is", "async", "await", "global", "nonlocal",
    "del", "assert", "match", "case", NULL
};

static const char *PY_KW2[] = {
    "None", "True", "False", "self", "cls", "int", "str", "list",
    "dict", "set", "tuple", "bool", "float", "bytes", "object",
    "print", "len", "range", "enumerate", "zip", "open", "super",
    "isinstance", "type", "Exception", "ValueError", "TypeError", NULL
};

static const char *JAVA_KW1[] = {
    "package", "import", "public", "private", "protected", "class",
    "interface", "enum", "record", "extends", "implements", "abstract",
    "final", "static", "synchronized", "volatile", "transient", "native",
    "strictfp", "new", "return", "if", "else", "for", "while", "do",
    "switch", "case", "default", "break", "continue", "try", "catch",
    "finally", "throw", "throws", "assert", "instanceof", "var", "yield",
    "sealed", "permits", NULL
};

static const char *JAVA_KW2[] = {
    "void", "boolean", "byte", "short", "int", "long", "float", "double",
    "char", "String", "Object", "Integer", "Long", "Double", "Float",
    "Boolean", "Character", "List", "ArrayList", "Map", "HashMap", "Set",
    "HashSet", "Optional", "System", "Override", "true", "false", "null",
    "this", "super", NULL
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
    if (has_ext(base, ".java"))
        return SYN_JAVA;
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
           strchr(",.()+-/*=~%<>[];:{}!&|^@", c) != NULL;
}

static int is_ident_start(unsigned char c) {
    return isalpha(c) || c == '_';
}

static int is_ident_char(unsigned char c) {
    return isalnum(c) || c == '_';
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

    if (b->syntax_id == SYN_NONE) {
        ln->hl_open_comment = ML_NONE;
        return;
    }

    const char **kw1 = NULL, **kw2 = NULL;
    const char *scs = NULL;
    const char *mcs = NULL, *mce = NULL;
    int has_preproc_hash = 0;
    int has_annotations = 0;
    int has_triple_quotes = 0;
    int has_funcs = 0;

    switch (b->syntax_id) {
        case SYN_C:
            kw1 = C_KW1; kw2 = C_KW2;
            scs = "//"; mcs = "/*"; mce = "*/";
            has_preproc_hash = 1;
            has_funcs = 1;
            break;
        case SYN_PYTHON:
            kw1 = PY_KW1; kw2 = PY_KW2;
            scs = "#";
            has_annotations = 1;
            has_triple_quotes = 1;
            has_funcs = 1;
            break;
        case SYN_JAVA:
            kw1 = JAVA_KW1; kw2 = JAVA_KW2;
            scs = "//"; mcs = "/*"; mce = "*/";
            has_annotations = 1;
            has_triple_quotes = 1;
            has_funcs = 1;
            break;
        case SYN_JS:
            kw1 = JS_KW1; kw2 = JS_KW2;
            scs = "//"; mcs = "/*"; mce = "*/";
            has_funcs = 1;
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
    int ml_state = (row > 0) ? b->lines[row - 1].hl_open_comment : ML_NONE;

    int i = 0;
    while (i < ln->size) {
        char c = ln->chars[i];
        unsigned char prev_hl = (i > 0) ? ln->hl[i - 1] : HL_NORMAL;

        /* Continue multi-line block comment */
        if (ml_state == ML_BLOCK_COMMENT && mce) {
            ln->hl[i] = HL_COMMENT;
            int mlen = (int)strlen(mce);
            if (i + mlen <= ln->size && strncmp(&ln->chars[i], mce, (size_t)mlen) == 0) {
                memset(&ln->hl[i], HL_COMMENT, (size_t)mlen);
                i += mlen;
                ml_state = ML_NONE;
                prev_sep = 1;
                continue;
            }
            i++;
            continue;
        }

        /* Continue multi-line triple-quoted string */
        if (ml_state == ML_TRIPLE_DQUOTE || ml_state == ML_TRIPLE_SQUOTE) {
            const char *tq = (ml_state == ML_TRIPLE_DQUOTE) ? "\"\"\"" : "'''";
            ln->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < ln->size) {
                ln->hl[i + 1] = HL_STRING;
                i += 2;
                continue;
            }
            if (i + 3 <= ln->size && strncmp(&ln->chars[i], tq, 3) == 0) {
                memset(&ln->hl[i], HL_STRING, 3);
                i += 3;
                ml_state = ML_NONE;
                prev_sep = 1;
                continue;
            }
            i++;
            continue;
        }

        /* Start multi-line block comment */
        if (mcs && !in_str && i + (int)strlen(mcs) <= ln->size &&
            strncmp(&ln->chars[i], mcs, strlen(mcs)) == 0) {
            int mlen = (int)strlen(mcs);
            memset(&ln->hl[i], HL_COMMENT, (size_t)mlen);
            i += mlen;
            ml_state = ML_BLOCK_COMMENT;
            continue;
        }

        /* Single-line comment */
        if (scs && !in_str && i + (int)strlen(scs) <= ln->size &&
            strncmp(&ln->chars[i], scs, strlen(scs)) == 0) {
            memset(&ln->hl[i], HL_COMMENT, (size_t)(ln->size - i));
            break;
        }

        /* C preprocessor (#include, #define, ...) */
        if (has_preproc_hash && prev_sep && c == '#' && !in_str) {
            memset(&ln->hl[i], HL_PREPROC, (size_t)(ln->size - i));
            break;
        }

        /* Java annotations & Python decorators (@Override, @dataclass, ...) */
        if (has_annotations && prev_sep && c == '@' && !in_str) {
            int j = i + 1;
            while (j < ln->size && (is_ident_char((unsigned char)ln->chars[j]) || ln->chars[j] == '.')) {
                j++;
            }
            memset(&ln->hl[i], HL_PREPROC, (size_t)(j - i));
            i = j;
            prev_sep = 0;
            continue;
        }

        /* Triple-quoted strings in Python / Java */
        if (has_triple_quotes && !in_str && i + 3 <= ln->size) {
            if (strncmp(&ln->chars[i], "\"\"\"", 3) == 0) {
                memset(&ln->hl[i], HL_STRING, 3);
                i += 3;
                ml_state = ML_TRIPLE_DQUOTE;
                continue;
            }
            if (b->syntax_id == SYN_PYTHON && strncmp(&ln->chars[i], "'''", 3) == 0) {
                memset(&ln->hl[i], HL_STRING, 3);
                i += 3;
                ml_state = ML_TRIPLE_SQUOTE;
                continue;
            }
        }

        /* Strings */
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

        /* Numbers (decimal, hex 0x..., binary 0b..., floats, C/Java suffixes) */
        if ((isdigit((unsigned char)c) && (prev_sep || prev_hl == HL_NUMBER)) ||
            (prev_hl == HL_NUMBER && strchr("xXbBaAcCdDeEfFlLuU._", c) != NULL)) {
            ln->hl[i] = HL_NUMBER;
            i++;
            prev_sep = 0;
            continue;
        }

        /* Keywords, types, and function names */
        if (prev_sep && is_ident_start((unsigned char)c)) {
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

            if (has_funcs) {
                int j = i;
                while (j < ln->size && is_ident_char((unsigned char)ln->chars[j])) j++;
                int k = j;
                while (k < ln->size && isspace((unsigned char)ln->chars[k])) k++;
                if (k < ln->size && ln->chars[k] == '(') {
                    memset(&ln->hl[i], HL_FUNC, (size_t)(j - i));
                    i = j;
                    prev_sep = 0;
                    continue;
                }
            }
        }

        prev_sep = is_sep(c);
        i++;
    }

    int changed = (ln->hl_open_comment != ml_state);
    ln->hl_open_comment = ml_state;
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
        case HL_FUNC:     return E.theme.hl_func;
        case HL_MATCH:    return E.theme.hl_match;
        case HL_NORMAL:
        default:          return E.theme.fg;
    }
}
