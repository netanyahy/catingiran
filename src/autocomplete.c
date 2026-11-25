#include "state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *word;
    const char *kind;
} AcEntry;

static const AcEntry PY_ENTRIES[] = {
    {"print()", "fn"}, {"input()", "fn"}, {"len()", "fn"}, {"range()", "fn"},
    {"enumerate()", "fn"}, {"zip()", "fn"}, {"isinstance()", "fn"},
    {"open()", "fn"}, {"sorted()", "fn"}, {"reversed()", "fn"},
    {"sum()", "fn"}, {"min()", "fn"}, {"max()", "fn"}, {"any()", "fn"},
    {"all()", "fn"}, {"abs()", "fn"}, {"round()", "fn"}, {"type()", "fn"},
    {"super()", "fn"}, {"str()", "type"}, {"int()", "type"},
    {"float()", "type"}, {"bool()", "type"}, {"list()", "type"},
    {"dict()", "type"}, {"set()", "type"}, {"tuple()", "type"},
    {"bytes()", "type"}, {"def ", "kw"}, {"class ", "kw"},
    {"import ", "kw"}, {"from ", "kw"}, {"return ", "kw"},
    {"async def ", "kw"}, {"await ", "kw"}, {"with ", "kw"},
    {"lambda ", "kw"}, {"yield ", "kw"}, {"raise ", "kw"},
    {"elif ", "kw"}, {"except ", "kw"}, {"finally:", "kw"},
    {"True", "const"}, {"False", "const"}, {"None", "const"},
    {"Exception", "exc"}, {"ValueError", "exc"}, {"TypeError", "exc"},
    {"KeyError", "exc"}, {"IndexError", "exc"},
    {"if __name__ == \"__main__\":", "snip"},
    {NULL, NULL}
};

static const AcEntry C_ENTRIES[] = {
    {"printf()", "fn"}, {"fprintf()", "fn"}, {"sprintf()", "fn"},
    {"snprintf()", "fn"}, {"scanf()", "fn"}, {"malloc()", "fn"},
    {"calloc()", "fn"}, {"realloc()", "fn"}, {"free()", "fn"},
    {"memcpy()", "fn"}, {"memmove()", "fn"}, {"memset()", "fn"},
    {"strlen()", "fn"}, {"strcmp()", "fn"}, {"strncmp()", "fn"},
    {"strchr()", "fn"}, {"strstr()", "fn"}, {"sizeof()", "kw"},
    {"#include <stdio.h>", "inc"}, {"#include <stdlib.h>", "inc"},
    {"#include <string.h>", "inc"}, {"#define ", "pre"},
    {"struct ", "kw"}, {"typedef ", "kw"}, {"enum ", "kw"},
    {"return ", "kw"}, {"static ", "kw"}, {"const ", "kw"},
    {"size_t", "type"}, {"ssize_t", "type"}, {"uint8_t", "type"},
    {"uint32_t", "type"}, {"uint64_t", "type"}, {"int32_t", "type"},
    {"FILE", "type"}, {"NULL", "const"},
    {"int main(int argc, char **argv)", "snip"},
    {NULL, NULL}
};

static const AcEntry JAVA_ENTRIES[] = {
    {"System.out.println()", "fn"}, {"System.out.print()", "fn"},
    {"public static void main(String[] args)", "snip"},
    {"public class ", "kw"}, {"public ", "kw"}, {"private ", "kw"},
    {"protected ", "kw"}, {"class ", "kw"}, {"interface ", "kw"},
    {"extends ", "kw"}, {"implements ", "kw"}, {"return ", "kw"},
    {"@Override", "ann"}, {"ArrayList<>", "type"}, {"HashMap<>", "type"},
    {"HashSet<>", "type"}, {"String", "type"}, {"Integer", "type"},
    {"Boolean", "type"}, {"Double", "type"}, {"Optional", "type"},
    {"Scanner", "type"}, {"List", "type"}, {"Map", "type"}, {"Set", "type"},
    {"Object", "type"}, {"true", "const"}, {"false", "const"}, {"null", "const"},
    {NULL, NULL}
};

static int is_ident(unsigned char c) {
    return isalnum(c) || c == '_' || c == '#' || c == '@';
}

static int ci_starts_with(const char *word, const char *prefix, int plen) {
    for (int i = 0; i < plen; i++) {
        if (!word[i]) return 0;
        if (tolower((unsigned char)word[i]) != tolower((unsigned char)prefix[i])) return 0;
    }
    return 1;
}

static void ac_add_item(const char *word, const char *kind, const char *prefix, int plen) {
    if (E.ac_count >= MAX_AC_ITEMS) return;
    if (!ci_starts_with(word, prefix, plen)) return;
    /* Skip exact identical single-word match without extra parens */
    if ((int)strlen(word) == plen && strncmp(word, prefix, (size_t)plen) == 0) return;

    for (int i = 0; i < E.ac_count; i++) {
        if (strcmp(E.ac_items[i], word) == 0) return;
    }
    snprintf(E.ac_items[E.ac_count], sizeof(E.ac_items[0]), "%s", word);
    snprintf(E.ac_kinds[E.ac_count], sizeof(E.ac_kinds[0]), "%s", kind);
    E.ac_count++;
}

void ac_dismiss(void) {
    E.ac_visible = 0;
    E.ac_count = 0;
    E.ac_sel = 0;
    E.ac_prefix_len = 0;
}

void ac_refresh(Buffer *b, int force) {
    if (!b || b->cy < 0 || b->cy >= b->num_lines) {
        ac_dismiss();
        return;
    }
    if (!force && !E.cfg.auto_complete) {
        ac_dismiss();
        return;
    }

    Line *ln = &b->lines[b->cy];
    int end = b->cx;
    if (end > ln->size) end = ln->size;
    int start = end;
    while (start > 0 && is_ident((unsigned char)ln->chars[start - 1])) {
        start--;
    }

    int plen = end - start;
    int min_ch = E.cfg.autocomplete_min_chars > 0 ? E.cfg.autocomplete_min_chars : 2;
    if (!force && plen < min_ch) {
        ac_dismiss();
        return;
    }
    if (force && plen == 0 && E.lsp_ac_count == 0) {
        ac_dismiss();
        return;
    }
    if (plen > 48) {
        ac_dismiss();
        return;
    }

    char prefix[64];
    if (plen > 0) memcpy(prefix, ln->chars + start, (size_t)plen);
    prefix[plen] = '\0';

    E.ac_count = 0;
    E.ac_sel = 0;
    E.ac_prefix_len = plen;

    /* Live LSP completion items take priority */
    for (int i = 0; i < E.lsp_ac_count && E.ac_count < MAX_AC_ITEMS; i++) {
        ac_add_item(E.lsp_ac_items[i],
                    E.lsp_ac_kinds[i][0] ? E.lsp_ac_kinds[i] : "lsp",
                    prefix, plen);
    }

    int syn = b->syntax_id;
    if (syn == SYN_PYTHON) {
        for (int i = 0; PY_ENTRIES[i].word; i++)
            ac_add_item(PY_ENTRIES[i].word, PY_ENTRIES[i].kind, prefix, plen);
    } else if (syn == SYN_C) {
        for (int i = 0; C_ENTRIES[i].word; i++)
            ac_add_item(C_ENTRIES[i].word, C_ENTRIES[i].kind, prefix, plen);
    } else if (syn == SYN_JAVA) {
        for (int i = 0; JAVA_ENTRIES[i].word; i++)
            ac_add_item(JAVA_ENTRIES[i].word, JAVA_ENTRIES[i].kind, prefix, plen);
    } else if (syn == SYN_NONE) {
        /* Helpful defaults even before filetype is detected */
        for (int i = 0; PY_ENTRIES[i].word; i++)
            ac_add_item(PY_ENTRIES[i].word, PY_ENTRIES[i].kind, prefix, plen);
        for (int i = 0; C_ENTRIES[i].word; i++)
            ac_add_item(C_ENTRIES[i].word, C_ENTRIES[i].kind, prefix, plen);
    }

    /* Words loaded from installed plugins */
    for (int i = 0; i < E.num_plugin_words; i++) {
        if (E.plugin_words[i].lang_id == 0 || E.plugin_words[i].lang_id == syn) {
            ac_add_item(E.plugin_words[i].word, E.plugin_words[i].kind, prefix, plen);
        }
    }

    /* Identifiers from current buffer */
    int max_scan = b->num_lines < 400 ? b->num_lines : 400;
    for (int r = 0; r < max_scan && E.ac_count < MAX_AC_ITEMS; r++) {
        Line *sl = &b->lines[r];
        int i = 0;
        while (i < sl->size && E.ac_count < MAX_AC_ITEMS) {
            if (isalpha((unsigned char)sl->chars[i]) || sl->chars[i] == '_') {
                int j = i;
                while (j < sl->size && (isalnum((unsigned char)sl->chars[j]) || sl->chars[j] == '_')) {
                    j++;
                }
                int wl = j - i;
                if (wl > plen && wl < 60) {
                    char wbuf[64];
                    memcpy(wbuf, sl->chars + i, (size_t)wl);
                    wbuf[wl] = '\0';
                    ac_add_item(wbuf, "var", prefix, plen);
                }
                i = j;
            } else {
                i++;
            }
        }
    }

    E.ac_visible = (E.ac_count > 0);
}

void ac_move(int delta) {
    if (!E.ac_visible || E.ac_count == 0) return;
    E.ac_sel = (E.ac_sel + delta + E.ac_count * 4) % E.ac_count;
}

void ac_accept(Buffer *b) {
    if (!E.ac_visible || E.ac_sel < 0 || E.ac_sel >= E.ac_count) return;
    const char *word = E.ac_items[E.ac_sel];
    int wlen = (int)strlen(word);

    for (int i = 0; i < E.ac_prefix_len; i++) {
        buf_backspace(b);
    }
    buf_insert_bytes(b, word, wlen);

    /* Place cursor inside () if completion ends with () */
    if (wlen >= 2 && word[wlen - 2] == '(' && word[wlen - 1] == ')') {
        b->cx--;
    }

    ac_dismiss();
}
