#include "state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
} JParser;

static void skip_ws(JParser *jp) {
    while (*jp->p && (*jp->p == ' ' || *jp->p == '\t' || *jp->p == '\n' || *jp->p == '\r')) {
        jp->p++;
    }
}

static JsonVal *jalloc(JsonType t) {
    JsonVal *v = calloc(1, sizeof(JsonVal));
    if (v) v->type = t;
    return v;
}

static char *parse_string(JParser *jp) {
    if (*jp->p != '"') return NULL;
    jp->p++;

    Abuf ab = {NULL, 0, 0};
    while (*jp->p && *jp->p != '"') {
        char c = *jp->p++;
        if (c == '\\' && *jp->p) {
            char esc = *jp->p++;
            switch (esc) {
                case '"':  ab_append(&ab, "\"", 1); break;
                case '\\': ab_append(&ab, "\\", 1); break;
                case '/':  ab_append(&ab, "/", 1); break;
                case 'b':  ab_append(&ab, "\b", 1); break;
                case 'f':  ab_append(&ab, "\f", 1); break;
                case 'n':  ab_append(&ab, "\n", 1); break;
                case 'r':  ab_append(&ab, "\r", 1); break;
                case 't':  ab_append(&ab, "\t", 1); break;
                case 'u': {
                    unsigned int cp = 0;
                    for (int i = 0; i < 4 && isxdigit((unsigned char)jp->p[i]); i++) {
                        char hc = jp->p[i];
                        cp <<= 4;
                        if (hc >= '0' && hc <= '9') cp |= (unsigned int)(hc - '0');
                        else if (hc >= 'a' && hc <= 'f') cp |= (unsigned int)(hc - 'a' + 10);
                        else cp |= (unsigned int)(hc - 'A' + 10);
                    }
                    for (int i = 0; i < 4 && *jp->p; i++) jp->p++;
                    if (cp < 0x80) {
                        char b = (char)cp;
                        ab_append(&ab, &b, 1);
                    } else if (cp < 0x800) {
                        char b[2] = {
                            (char)(0xC0 | (cp >> 6)),
                            (char)(0x80 | (cp & 0x3F))
                        };
                        ab_append(&ab, b, 2);
                    } else {
                        char b[3] = {
                            (char)(0xE0 | (cp >> 12)),
                            (char)(0x80 | ((cp >> 6) & 0x3F)),
                            (char)(0x80 | (cp & 0x3F))
                        };
                        ab_append(&ab, b, 3);
                    }
                    break;
                }
                default:
                    ab_append(&ab, &esc, 1);
                    break;
            }
        } else {
            ab_append(&ab, &c, 1);
        }
    }
    if (*jp->p == '"') jp->p++;
    ab_append(&ab, "\0", 1);
    return ab.b ? ab.b : strdup("");
}

static JsonVal *parse_val(JParser *jp);

static JsonVal *parse_array(JParser *jp) {
    jp->p++; /* '[' */
    JsonVal *v = jalloc(J_ARR);
    skip_ws(jp);
    if (*jp->p == ']') {
        jp->p++;
        return v;
    }
    int cap = 0;
    while (*jp->p) {
        skip_ws(jp);
        JsonVal *elem = parse_val(jp);
        if (!elem) break;
        if (v->count >= cap) {
            cap = cap ? cap * 2 : 8;
            v->items = realloc(v->items, (size_t)cap * sizeof(JsonVal *));
        }
        v->items[v->count++] = elem;
        skip_ws(jp);
        if (*jp->p == ',') {
            jp->p++;
        } else if (*jp->p == ']') {
            jp->p++;
            break;
        } else {
            break;
        }
    }
    return v;
}

static JsonVal *parse_object(JParser *jp) {
    jp->p++; /* '{' */
    JsonVal *v = jalloc(J_OBJ);
    skip_ws(jp);
    if (*jp->p == '}') {
        jp->p++;
        return v;
    }
    int cap = 0;
    while (*jp->p) {
        skip_ws(jp);
        if (*jp->p != '"') break;
        char *k = parse_string(jp);
        skip_ws(jp);
        if (*jp->p == ':') jp->p++;
        skip_ws(jp);
        JsonVal *val = parse_val(jp);
        if (!val) {
            free(k);
            break;
        }
        if (v->count >= cap) {
            cap = cap ? cap * 2 : 8;
            v->keys = realloc(v->keys, (size_t)cap * sizeof(char *));
            v->items = realloc(v->items, (size_t)cap * sizeof(JsonVal *));
        }
        v->keys[v->count] = k;
        v->items[v->count] = val;
        v->count++;
        skip_ws(jp);
        if (*jp->p == ',') {
            jp->p++;
        } else if (*jp->p == '}') {
            jp->p++;
            break;
        } else {
            break;
        }
    }
    return v;
}

static JsonVal *parse_val(JParser *jp) {
    skip_ws(jp);
    char c = *jp->p;
    if (!c) return NULL;
    if (c == '{') return parse_object(jp);
    if (c == '[') return parse_array(jp);
    if (c == '"') {
        JsonVal *v = jalloc(J_STR);
        v->str_val = parse_string(jp);
        return v;
    }
    if (strncmp(jp->p, "true", 4) == 0) {
        jp->p += 4;
        JsonVal *v = jalloc(J_BOOL);
        v->bool_val = 1;
        return v;
    }
    if (strncmp(jp->p, "false", 5) == 0) {
        jp->p += 5;
        JsonVal *v = jalloc(J_BOOL);
        v->bool_val = 0;
        return v;
    }
    if (strncmp(jp->p, "null", 4) == 0) {
        jp->p += 4;
        return jalloc(J_NULL);
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        char *endp = NULL;
        double d = strtod(jp->p, &endp);
        if (endp != jp->p) {
            jp->p = endp;
            JsonVal *v = jalloc(J_NUM);
            v->num_val = d;
            return v;
        }
    }
    return NULL;
}

JsonVal *json_parse(const char *s) {
    if (!s) return NULL;
    JParser jp = {s};
    return parse_val(&jp);
}

void json_free(JsonVal *v) {
    if (!v) return;
    if (v->type == J_STR) {
        free(v->str_val);
    } else if (v->type == J_ARR) {
        for (int i = 0; i < v->count; i++) json_free(v->items[i]);
        free(v->items);
    } else if (v->type == J_OBJ) {
        for (int i = 0; i < v->count; i++) {
            free(v->keys[i]);
            json_free(v->items[i]);
        }
        free(v->keys);
        free(v->items);
    }
    free(v);
}

JsonVal *json_get(JsonVal *obj, const char *key) {
    if (!obj || obj->type != J_OBJ || !key) return NULL;
    for (int i = 0; i < obj->count; i++) {
        if (obj->keys[i] && strcmp(obj->keys[i], key) == 0) {
            return obj->items[i];
        }
    }
    return NULL;
}

const char *json_str(JsonVal *v) {
    return (v && v->type == J_STR) ? v->str_val : NULL;
}

double json_num(JsonVal *v, double def) {
    return (v && v->type == J_NUM) ? v->num_val : def;
}

char *json_escape(const char *s) {
    if (!s) return strdup("\"\"");
    Abuf ab = {NULL, 0, 0};
    ab_append(&ab, "\"", 1);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  ab_append_str(&ab, "\\\""); break;
            case '\\': ab_append_str(&ab, "\\\\"); break;
            case '\b': ab_append_str(&ab, "\\b"); break;
            case '\f': ab_append_str(&ab, "\\f"); break;
            case '\n': ab_append_str(&ab, "\\n"); break;
            case '\r': ab_append_str(&ab, "\\r"); break;
            case '\t': ab_append_str(&ab, "\\t"); break;
            default:
                if (c < 0x20) {
                    ab_append_fmt(&ab, "\\u%04x", c);
                } else {
                    ab_append(&ab, (const char *)&c, 1);
                }
                break;
        }
    }
    ab_append(&ab, "\"\0", 2);
    return ab.b;
}
