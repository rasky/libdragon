#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "../common/stb_ds.h"
#include "n64sym_compact.h"

// Remove all occurrences of token in-place.
static void strip_token(char *buf, const char *token)
{
    size_t tlen = strlen(token);
    char *p;
    while ((p = strstr(buf, token)) != NULL) {
        memmove(p, p + tlen, strlen(p + tlen) + 1);
    }
}

// Replace anonymous namespace marker
static void normalize_anonymous_ns(char *buf)
{
    const char *needle = "(anonymous namespace)";
    size_t nlen = strlen(needle);
    char *p;
    while ((p = strstr(buf, needle)) != NULL) {
        memmove(p + 6, p + nlen, strlen(p + nlen) + 1); // 6 = len("(anon)")
        memcpy(p, "(anon)", 6);
    }
}

// Replace all occurrences of "from" with shorter/same-sized "to" in-place.
static void replace_token(char *buf, const char *from, const char *to)
{
    size_t flen = strlen(from);
    size_t tlen = strlen(to);
    if (tlen > flen)
        return; // We only support shrinking/same-size replacements.
    char *p = buf;
    while ((p = strstr(p, from)) != NULL) {
        memmove(p + tlen, p + flen, strlen(p + flen) + 1);
        memcpy(p, to, tlen);
        p += tlen;
    }
}

static bool is_boundary_char(char c)
{
    return c == 0 || isspace((unsigned char)c) || c == '<' || c == '>' ||
           c == '(' || c == ')' || c == ',' || c == '&' || c == '*' ||
           c == ':' || c == '[' || c == ']' || c == '+' || c == '-' || c == '~' ||
           c == '{' || c == '}' || c == '|';
}

// Replace tokens only if surrounded by boundaries (to avoid touching identifiers like "print").
static void replace_token_word(char *buf, const char *from, const char *to)
{
    size_t flen = strlen(from);
    size_t tlen = strlen(to);
    if (tlen > flen)
        return;
    bool relaxed_after = (flen > 0 && isspace((unsigned char)from[flen - 1]));
    char *p = buf;
    while ((p = strstr(p, from)) != NULL) {
        char before = (p == buf) ? 0 : p[-1];
        char after = p[flen];
        if (!is_boundary_char(before))
            { p += flen; continue; }
        if (!relaxed_after && !is_boundary_char(after))
            { p += flen; continue; }
        memmove(p + tlen, p + flen, strlen(p + flen) + 1);
        memcpy(p, to, tlen);
        p += tlen;
    }
}

// Remove space after comma and before * / &, collapse double spaces.
static void tighten_spacing(char *buf)
{
    char *r = buf, *w = buf;
    bool last_space = false;
    while (*r) {
        if (*r == ',' && r[1] == ' ') {
            *w++ = *r++;
            r++; // skip space
            last_space = false;
            continue;
        }
        if (*r == ' ' && (r[1] == '*' || r[1] == '&')) {
            r++; // skip space before pointer/ref
            last_space = false;
            continue;
        }
        if (*r == ' ') {
            if (last_space) {
                r++;
                continue;
            }
            last_space = true;
        } else {
            last_space = false;
        }
        *w++ = *r++;
    }
    *w = 0;
}

// Append helper for stbds char buffer.
static void buf_append_range(char **out, const char *s, int len)
{
    for (int i = 0; i < len; i++) stbds_arrput(*out, s[i]);
}

// Abbreviate a namespace component for middle elements.
static void append_abbrev(char **out, const char *s, int len)
{
    if (strncmp(s, "__detail", 8) == 0) {
        buf_append_range(out, "detail", 6);
    } else if (s[0] == '_') {
        buf_append_range(out, s, 2);
    } else {
        buf_append_range(out, s, 1);
    }
}

static void trim_range(const char **s, int *len)
{
    while (*len > 0 && isspace((unsigned char)(*s)[0])) {
        (*s)++; (*len)--;
    }
    while (*len > 0 && isspace((unsigned char)(*s)[*len - 1])) {
        (*len)--;
    }
}

// Compress namespace chains: for 2 components, abbreviate the first, keep the last.
// For 3 components, abbreviate the first two, keep the last.
// For >=4 components, keep first and last two, abbreviate the middle ones.
static void shrink_namespaces(char *buf)
{
    const char *p = buf;
    char *out = NULL;
    while (*p) {
        // Detect start of identifier
        const char *tok_start = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '$'))
            p++;
        if (tok_start == p) {
            stbds_arrput(out, *p++);
            continue;
        }

        // Collect chain of identifiers separated by ::
        const char *chain_start = tok_start;
        struct token { const char *s; int len; } toks[32];
        int ntok = 0;
        toks[ntok++] = (struct token){tok_start, (int)(p - tok_start)};
        while (p[0] == ':' && p[1] == ':' && ntok < 32) {
            p += 2;
            const char *ts = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '$'))
                p++;
            if (ts == p) break;
            toks[ntok++] = (struct token){ts, (int)(p - ts)};
        }

        if (ntok == 2) {
            int upper = 0;
            for (int i = 0; i < toks[0].len; i++)
                if (isupper((unsigned char)toks[0].s[i])) upper++;
            // Do not abbreviate very short prefixes with multiple caps (eg. MiMem)
            if (toks[0].len <= 5 && upper >= 2) {
                buf_append_range(&out, chain_start, (int)(p - chain_start));
            } else {
                append_abbrev(&out, toks[0].s, toks[0].len);
                stbds_arrput(out, ':'); stbds_arrput(out, ':');
                buf_append_range(&out, toks[1].s, toks[1].len);
            }
        } else if (ntok == 3) {
            append_abbrev(&out, toks[0].s, toks[0].len);
            stbds_arrput(out, ':'); stbds_arrput(out, ':');
            if (toks[1].len == 6 && strncmp(toks[1].s, "detail", 6) == 0)
                buf_append_range(&out, toks[1].s, toks[1].len);
            else
                append_abbrev(&out, toks[1].s, toks[1].len);
            stbds_arrput(out, ':'); stbds_arrput(out, ':');
            buf_append_range(&out, toks[2].s, toks[2].len);
        } else if (ntok >= 4) {
            buf_append_range(&out, toks[0].s, toks[0].len);
            for (int i = 1; i < ntok; i++) {
                stbds_arrput(out, ':'); stbds_arrput(out, ':');
                if (i >= ntok - 2)
                    buf_append_range(&out, toks[i].s, toks[i].len);
                else
                    append_abbrev(&out, toks[i].s, toks[i].len);
            }
        } else {
            buf_append_range(&out, chain_start, (int)(p - chain_start));
        }
    }
    stbds_arrput(out, 0);
    strcpy(buf, out);
    stbds_arrfree(out);
}

// For certain containers, keep only first and last template argument.
static void shrink_container_templates(char *buf)
{
    static const char *containers[] = { "umap", "unordered_map", "map", NULL };
    const char *p = buf;
    const char *emit_start = buf;
    char *out = NULL;

    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *name_start = p;
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '$') p++;
            const char *name_end = p;
            bool match = false;
            if (*p == '<') {
                int name_len = (int)(name_end - name_start);
                for (int i = 0; containers[i]; i++) {
                    if ((int)strlen(containers[i]) == name_len &&
                        strncmp(name_start, containers[i], name_len) == 0) {
                        match = true; break;
                    }
                }
            }
            if (match) {
                const char *tpl = p; // at '<'
                int depth = 1;
                const char *arg_start = tpl + 1;
                struct { const char *s; int len; } *args = NULL;
                const char *q = tpl + 1;
                while (*q && depth > 0) {
                    if (*q == '<') depth++;
                    else if (*q == '>') depth--;
                    if ((depth == 1 && *q == ',') || (depth == 0 && *q == '>')) {
                        int len = (int)(q - arg_start);
                        const char *as = arg_start;
                        trim_range(&as, &len);
                        stbds_arrput(args, ((typeof(*args)){as, len}));
                        arg_start = q + 1;
                    }
                    q++;
                }
                if (depth == 0 && stbds_arrlen(args) >= 2) {
                    int last_idx = -1;
                    for (int i = (int)stbds_arrlen(args) - 1; i >= 0; i--) {
                        if (args[i].len > 0) { last_idx = i; break; }
                    }
                    if (last_idx < 1 || args[0].len == 0) {
                        stbds_arrfree(args);
                        p = name_end;
                        continue;
                    }
                    // Emit prefix
                    buf_append_range(&out, emit_start, (int)(name_start - emit_start));
                    // Emit name and reduced args
                    buf_append_range(&out, name_start, (int)(name_end - name_start));
                    stbds_arrput(out, '<');
                    buf_append_range(&out, args[0].s, args[0].len);
                    buf_append_range(&out, ",...,", 5);
                    // Choose second arg for maps, last arg otherwise
                    int idx = 0;
                    if ((name_end - name_start == 4 && strncmp(name_start, "umap", 4) == 0) ||
                        (name_end - name_start == 13 && strncmp(name_start, "unordered_map", 13) == 0) ||
                        (name_end - name_start == 3 && strncmp(name_start, "map", 3) == 0)) {
                        idx = 1;
                    } else {
                        idx = last_idx;
                    }
                    const char *as = args[idx].s;
                    int alen = args[idx].len;
                    // For map-like, drop leading namespaces keeping final component of value type.
                    if (idx == 1) {
                        const char *sep = NULL;
                        for (int k = 0; k < alen - 1; k++) {
                            if (as[k] == ':' && as[k+1] == ':') sep = as + k + 2;
                        }
                        if (sep) {
                            alen -= (int)(sep - as);
                            as = sep;
                        }
                    }
                    buf_append_range(&out, as, alen);
                    stbds_arrput(out, '>');
                    emit_start = q;
                    stbds_arrfree(args);
                    p = q;
                    continue;
                }
                stbds_arrfree(args);
            }
            p = name_end;
            continue;
        }
        p++;
    }
    buf_append_range(&out, emit_start, (int)(strlen(emit_start)));
    stbds_arrput(out, 0);
    strcpy(buf, out);
    stbds_arrfree(out);
}

// For normal_iterators, keep only first template arg (the pointer).
static void shrink_iterator_templates(char *buf)
{
    const char *p = buf;
    const char *emit_start = buf;
    char *out = NULL;
    while ((p = strstr(p, "iter")) != NULL) {
        // ensure we're at identifier boundary
        if (p != buf && (isalnum((unsigned char)p[-1]) || p[-1]=='_' || p[-1]=='$')) { p++; continue; }
        const char *name_start = p;
        while (isalnum((unsigned char)*p) || *p == '_' || *p == '$') p++;
        const char *name_end = p;
        if (*p != '<') continue;
        const char *tpl = p;
        int depth = 1;
        const char *arg_start = tpl + 1;
        const char *q = tpl + 1;
        const char *first_s = NULL; int first_len = 0;
        while (*q && depth > 0) {
            if (*q == '<') depth++;
            else if (*q == '>') depth--;
            if ((depth == 1 && *q == ',') || (depth == 0 && *q == '>')) {
                if (!first_s) {
                    int len = (int)(q - arg_start);
                    const char *as = arg_start;
                    trim_range(&as, &len);
                    first_s = as; first_len = len;
                }
                if (depth == 0) { q++; break; }
                arg_start = q + 1;
            }
            q++;
        }
        if (!first_s || depth != 0) continue;
        // Drop leading s:: before fn to save space
        const char *prefix_end = name_start;
        if (name_start - emit_start >= 3 && strncmp(name_start - 3, "s::", 3) == 0)
            prefix_end = name_start - 3;
        buf_append_range(&out, emit_start, (int)(prefix_end - emit_start));
        buf_append_range(&out, name_start, (int)(name_end - name_start));
        stbds_arrput(out, '<');
        buf_append_range(&out, first_s, first_len);
        stbds_arrput(out, '>');
        emit_start = q;
    }
    buf_append_range(&out, emit_start, (int)strlen(emit_start));
    stbds_arrput(out, 0);
    strcpy(buf, out);
    stbds_arrfree(out);
}

// For niter (node iterators), keep only the first template argument.
static void shrink_niter_templates(char *buf)
{
    const char *p = buf;
    const char *emit_start = buf;
    char *out = NULL;
    while ((p = strstr(p, "niter")) != NULL) {
        if (p != buf && (isalnum((unsigned char)p[-1]) || p[-1]=='_' || p[-1]=='$')) { p++; continue; }
        const char *name_start = p;
        while (isalnum((unsigned char)*p) || *p == '_' || *p == '$') p++;
        if (*p != '<') continue;
        const char *tpl = p;
        int depth = 1;
        const char *arg_start = tpl + 1;
        const char *q = tpl + 1;
        const char *first_s = NULL; int first_len = 0;
        while (*q && depth > 0) {
            if (*q == '<') depth++;
            else if (*q == '>') depth--;
            if ((depth == 1 && *q == ',') || (depth == 0 && *q == '>')) {
                if (!first_s) {
                    int len = (int)(q - arg_start);
                    const char *as = arg_start;
                    trim_range(&as, &len);
                    first_s = as; first_len = len;
                }
                if (depth == 0) { q++; break; }
                arg_start = q + 1;
            }
            q++;
        }
        if (!first_s || depth != 0) continue;
        buf_append_range(&out, emit_start, (int)(name_start - emit_start));
        buf_append_range(&out, name_start, (int)(p - name_start));
        stbds_arrput(out, '<');
        buf_append_range(&out, first_s, first_len);
        stbds_arrput(out, '>');
        emit_start = q;
    }
    buf_append_range(&out, emit_start, (int)strlen(emit_start));
    stbds_arrput(out, 0);
    strcpy(buf, out);
    stbds_arrfree(out);
}

// For insert<...>(...) shorten template args to ellipsis.
static void shrink_insert_templates(char *buf)
{
    const char *p = buf;
    const char *emit_start = buf;
    char *out = NULL;
    while ((p = strstr(p, "insert<")) != NULL) {
        const char *name_start = p;
        const char *tpl = p + strlen("insert");
        const char *q = tpl + 1; // after '<'
        int depth = 1;
        while (*q && depth > 0) {
            if (*q == '<') depth++;
            else if (*q == '>') depth--;
            q++;
        }
        if (depth != 0) { p++; continue; }
        // q points after '>'
        buf_append_range(&out, emit_start, (int)(name_start - emit_start));
        buf_append_range(&out, "insert(...)", 11);
        emit_start = q;
        p = q;
    }
    buf_append_range(&out, emit_start, (int)strlen(emit_start));
    stbds_arrput(out, 0);
    strcpy(buf, out);
    stbds_arrfree(out);
}

// For std::_Function_handler, keep only return type and callable target.
static void shrink_function_handler(char *buf)
{
    const char *needle = "fn";
    const char *p = buf;
    const char *emit_start = buf;
    char *out = NULL;
    while ((p = strstr(p, needle)) != NULL) {
        if (p != buf && (isalnum((unsigned char)p[-1]) || p[-1]=='_' || p[-1]=='$')) { p++; continue; }
        const char *name_start = p;
        p += strlen(needle);
        if (*p != '<') continue;
        const char *tpl = p;
        int depth = 1;
        const char *arg_start = tpl + 1;
        const char *q = tpl + 1;
        const char *args[2] = {NULL, NULL};
        int arglen[2] = {0, 0};
        int found = 0;
        while (*q && depth > 0) {
            if (*q == '<') depth++;
            else if (*q == '>') depth--;
            if ((depth == 1 && *q == ',') || (depth == 0 && *q == '>')) {
                if (found < 2) {
                    const char *as = arg_start;
                    int len = (int)(q - arg_start);
                    trim_range(&as, &len);
                    while (len > 0 && as[len-1] == '}') len--;
                    args[found] = as;
                    arglen[found] = len;
                }
                found++;
                arg_start = q + 1;
            }
            q++;
        }
        if (depth != 0 || !args[0]) continue;
        // Trim first arg to return type if it's a function type like "void (ul)"
        const char *ret_s = args[0];
        int ret_len = arglen[0];
        for (int k = 0; k < arglen[0]; k++) {
            if (args[0][k] == '(') { ret_len = k; break; }
        }
        while (ret_len > 0 && isspace((unsigned char)ret_s[ret_len-1])) ret_len--;

        buf_append_range(&out, emit_start, (int)(name_start - emit_start));
        buf_append_range(&out, name_start, (int)(p - name_start));
        stbds_arrput(out, '<');
        buf_append_range(&out, ret_s, ret_len);
        if (args[1]) {
            const char *a1 = args[1];
            int a1len = arglen[1];
            if (a1len > 4 && strncmp(a1, "s::_", 4) == 0) { a1 += 3; a1len -= 3; }
            while (a1len > 0 && a1[a1len-1] == '&') a1len--;
            // If the callable has namespaces, keep only the tail component.
            const char *last = NULL;
            for (int k = 0; k + 1 < a1len; k++) {
                if (a1[k] == ':' && a1[k+1] == ':')
                    last = a1 + k + 2;
            }
            if (last) {
                a1len = (int)(a1 + a1len - last);
                a1 = last;
            }
            buf_append_range(&out, ",", 1);
            buf_append_range(&out, a1, a1len);
        }
        stbds_arrput(out, '>');
        emit_start = q;
    }
    buf_append_range(&out, emit_start, (int)strlen(emit_start));
    stbds_arrput(out, 0);
    strcpy(buf, out);
    stbds_arrfree(out);
}

// For copy_move_a, keep only the first template arg (bool) plus ellipsis.
static void shrink_copy_move_templates(char *buf)
{
    const char *needle = "copy_move";
    const char *p = buf;
    const char *emit_start = buf;
    char *out = NULL;
    while ((p = strstr(p, needle)) != NULL) {
        if (p != buf && (isalnum((unsigned char)p[-1]) || p[-1]=='_' || p[-1]=='$')) { p++; continue; }
        const char *name_start = p;
        p += strlen(needle);
        if (*p != '<') continue;
        const char *tpl = p;
        int depth = 1;
        const char *arg_start = tpl + 1;
        const char *q = tpl + 1;
        const char *first_s = NULL; int first_len = 0;
        while (*q && depth > 0) {
            if (*q == '<') depth++;
            else if (*q == '>') depth--;
            if ((depth == 1 && *q == ',') || (depth == 0 && *q == '>')) {
                if (!first_s) {
                    int len = (int)(q - arg_start);
                    const char *as = arg_start;
                    trim_range(&as, &len);
                    first_s = as; first_len = len;
                }
                if (depth == 0) { q++; break; }
                arg_start = q + 1;
            }
            q++;
        }
        if (!first_s || depth != 0) continue;
        buf_append_range(&out, emit_start, (int)(name_start - emit_start));
        buf_append_range(&out, name_start, (int)(p - name_start));
        stbds_arrput(out, '<');
        buf_append_range(&out, first_s, first_len);
        stbds_arrput(out, '>');
        emit_start = q;
    }
    buf_append_range(&out, emit_start, (int)strlen(emit_start));
    stbds_arrput(out, 0);
    strcpy(buf, out);
    stbds_arrfree(out);
}

// If a template argument list is very long, keep only the first arg.
static void shrink_templates(char *buf, int threshold)
{
    if (strstr(buf, "fn<")) return; // keep function_handler templates intact
    if (strstr(buf, ",...,")) return; // already compacted container args
    char *start = NULL;
    int depth = 0;
    for (char *p = buf; *p; p++) {
        if (*p == '<') {
            if (depth == 0) {
                // Skip compressing iterator-like templates for readability
                const char *n = p - 1;
                while (n >= buf && (isalnum((unsigned char)*n) || *n == '_' || *n == '$')) n--;
                n++;
                if (n < p && strstr(n, "iter"))
                    { depth++; continue; }
                start = p;
            }
            depth++;
        } else if (*p == '>') {
            depth--;
            if (depth == 0 && start) {
                char *end = p;
                if ((end - start + 1) > threshold) {
                    int local_depth = 1;
                    char *comma = NULL;
                    for (char *c = start + 1; c < end; c++) {
                        if (*c == '<') local_depth++;
                        else if (*c == '>') local_depth--;
                        else if (*c == ',' && local_depth == 1) { comma = c; break; }
                    }
                    if (comma && comma > start + 1) {
                        char *arg_end = comma;
                        while (arg_end > start + 1 && isspace((unsigned char)arg_end[-1])) arg_end--;
                        size_t keep = arg_end - (start + 1);
                        const char *suffix = ",...>";
                        size_t tail_len = strlen(end + 1) + 1;
                        memmove(start + 1 + keep + strlen(suffix), end + 1, tail_len);
                        memcpy(start + 1 + keep, suffix, strlen(suffix));
                    } else {
                        const char *middle = "...>";
                        size_t tail_len = strlen(end + 1) + 1;
                        memmove(start + 1 + strlen(middle), end + 1, tail_len);
                        memcpy(start + 1, middle, strlen(middle));
                    }
                }
                start = NULL;
            }
        }
    }
}

// Remove or shrink parameter lists if still too long.
static void shrink_params(char *buf, bool keep_first, int threshold)
{
    bool allow_two = keep_first && (strstr(buf, "copy_move") == NULL);
    int angle = 0, par = 0;
    char *open = NULL;
    for (char *p = buf; *p; p++) {
        if (*p == '<') angle++;
        else if (*p == '>') angle = angle > 0 ? angle - 1 : 0;
        else if (*p == '(' && angle == 0) {
            if (par == 0) open = p;
            par++;
        } else if (*p == ')' && angle == 0) {
            if (par > 0) par--;
            if (par == 0 && open) {
                char *close = p;
                if ((close - open + 1) > threshold) {
                    if (keep_first) {
                        int depth = 0;
                        const char *args[2] = {NULL, NULL};
                        int arglen[2] = {0, 0};
                        const char *arg_start = open + 1;
                        int found = 0;
                        for (char *c = open + 1; c <= close; c++) {
                            if (*c == '(') depth++;
                            else if (*c == ')') depth--;
                            else if ((*c == ',' && depth == 0) || (c == close)) {
                                if (found == 0 || (found == 1 && allow_two)) {
                                    const char *as = arg_start;
                                    int len = (int)(c - arg_start);
                                    trim_range(&as, &len);
                                    args[found] = as;
                                    arglen[found] = len;
                                }
                                found++;
                                arg_start = c + 1;
                            }
                        }
                        if (args[0]) {
                            size_t tail_len = strlen(close + 1) + 1;
                            char *dst = open;
                            *dst++ = '(';
                            memcpy(dst, args[0], arglen[0]); dst += arglen[0];
                            if (args[1]) {
                                *dst++ = ',';
                                memcpy(dst, args[1], arglen[1]); dst += arglen[1];
                            }
                            memcpy(dst, ",...)", 5); dst += 5;
                            memmove(dst, close + 1, tail_len);
                        } else {
                            const char *suffix = "...)";
                            size_t tail_len = strlen(close + 1) + 1;
                            memmove(open + 1 + strlen(suffix), close + 1, tail_len);
                            memcpy(open + 1, suffix, strlen(suffix));
                        }
                    } else {
                        int depth = 0;
                        char *comma = NULL;
                        for (char *c = open + 1; c < close; c++) {
                            if (*c == '(') depth++;
                            else if (*c == ')') depth--;
                            else if (*c == ',' && depth == 0) { comma = c; break; }
                        }
                        if (comma) {
                            char *arg_end = comma;
                            while (arg_end > open + 1 && isspace((unsigned char)arg_end[-1])) arg_end--;
                            size_t keep = arg_end - (open + 1);
                            const char *suffix = ",...)";
                            size_t tail_len = strlen(close + 1) + 1;
                            memmove(open + 1 + keep + strlen(suffix), close + 1, tail_len);
                            memcpy(open + 1 + keep, suffix, strlen(suffix));
                        } else {
                            const char *suffix = "(...)";
                            size_t tail_len = strlen(close + 1) + 1;
                            memmove(open + strlen(suffix), close + 1, tail_len);
                            memcpy(open, suffix, strlen(suffix));
                        }
                    }
                }
                open = NULL;
            }
        }
    }
}

void head_tail_ellipsis(char *buf, int max_len)
{
    size_t len = strlen(buf);
    if (max_len <= 0) { buf[0] = 0; return; }
    if ((int)len <= max_len) return;
    if (max_len <= 3) { buf[max_len] = 0; return; }
    int tail = max_len / 4;
    if (tail < 8) tail = 8;
    if (tail + 3 >= max_len) tail = max_len - 4;
    int head = max_len - tail - 3;
    size_t tail_len = (size_t)tail;
    size_t total_len = strlen(buf);
    if (tail_len > total_len) tail_len = total_len;
    memmove(buf + head + 3, buf + total_len - tail_len, tail_len);
    memcpy(buf + head, "...", 3);
    buf[head + 3 + tail_len] = 0;
}

static void strip_throw_specs(char *buf)
{
    const char *needle = " throw(";
    char *p = strstr(buf, needle);
    while (p) {
        int depth = 1;
        char *q = p + strlen(needle);
        while (*q && depth) {
            if (*q == '(') depth++;
            else if (*q == ')') depth--;
            q++;
        }
        memmove(p, q, strlen(q) + 1);
        p = strstr(buf, needle);
    }
}

static void strip_trailing_const(char *buf)
{
    char *p = strstr(buf, ") const");
    while (p) {
        memmove(p + 1, p + 7, strlen(p + 7) + 1);
        p = strstr(buf, ") const");
    }
    size_t len = strlen(buf);
    if (len >= 6 && strcmp(buf + len - 6, " const") == 0)
        buf[len - 6] = 0;
}

static void normalize_lambdas(char *buf)
{
    replace_token(buf, "{lambda", "lambda");
    replace_token(buf, "<lambda", "lambda");
    // Collapse lambda(...) -> lambda, preserving trailing #n if present.
    char *p = buf;
    while ((p = strstr(p, "lambda")) != NULL) {
        char *q = p + strlen("lambda");
        if (*q == '(') {
            int depth = 1;
            char *r = q + 1;
            while (*r && depth) {
                if (*r == '(') depth++;
                else if (*r == ')') depth--;
                r++;
            }
            // r points after ')'
            char *hash = r;
            if (*hash == '#') {
                hash++;
                while (*hash && isdigit((unsigned char)*hash)) hash++;
            }
            memmove(q, r, strlen(r) + 1);
            // Remove a trailing '}' after lambda#n if present.
            if (*q == '}' && q > buf && q[-1] == '#') memmove(q, q + 1, strlen(q + 1) + 1);
            if (*q == '}' && q > buf && isdigit((unsigned char)q[-1])) memmove(q, q + 1, strlen(q + 1) + 1);
        }
        p++;
    }
}

static void apply_type_abbrev(char *buf)
{
    static const struct { const char *from, *to; } repl[] = {
        {"std::__detail::_Node_iterator", "niter"},
        {"std::__detail::niter", "niter"},
        {"s::detail::", ""},
        {"std::pair<", "pair<"},
        {"s::pair<", "pair<"},
        {"__gnu_cxx::", "g::"},
        {"unordered_map", "umap"},
        {"__new_allocator", "new_alloc"},
        {"__normal_iterator", "niter"},
        {"normal_iterator", "niter"},
        {"g::niter", "iter"},
        {"__copy_move_a", "copy_move"},
        {"copy_move_a", "copy_move"},
        {"std::fn", "fn"},
        {"s::fn", "fn"},
        {"s::detail::niter", "niter"},
        {"detail::niter", "niter"},
        {"s::__detail::niter", "niter"},
        {"__detail::niter", "niter"},
        {"s::insert", " insert"},
        {"_Node_iterator", "niter"},
        {"_Insert", "insert"},
        {"s::detail::insert", "insert"},
        {"std::__detail::insert", "insert"},
        {"_Any const&", "_Any"},
        {"const&_Any", "_Any"},
        {"const &_Any", "_Any"},
        {"ul&&", "ul"},
        {"_Function_handler", "fn"},
        {"_Any_data", "_Any"},
        {"unsigned long long", "ull"},
        {"ul&&", "ul"},
        {"long long", "ll"},
        {"unsigned long", "ul"},
        {"long double", "ld"},
        {"unsigned int", "ui"},
        {"unsigned short", "us"},
        {"unsigned char", "uc"},
        {"unsigned ", "u"},
        {"long", "l"},
        {"double", "d"},
        {"float", "f"},
        {"int", "i"},
        {"short", "s"},
        {"char", "c"},
        {"bool", "b"},
        {"std::", "s::"},
        {"s::string", "s::str"},
        {"string", "str"},
        {"_Hashtable_traits", "Ht"},
        {"__detail", "detail"},
        {"basic_string", "str"},
        {"allocator", "alloc"},
        {"vector", "vec"},
        {"map", "map"},
        {NULL, NULL},
    };
    for (int i = 0; repl[i].from; i++) {
        if (strstr(repl[i].from, "::"))
            replace_token(buf, repl[i].from, repl[i].to);
        else
            replace_token_word(buf, repl[i].from, repl[i].to);
    }
}

char *simple_truncate(const char *s, int max_len)
{
    int slen = (int)strlen(s);
    int len = slen < max_len ? slen : max_len;
    char *out = strndup(s, len);
    if (!out) return NULL;
    if ((int)strlen(s) > max_len && max_len >= 3)
        strcpy(out + max_len - 3, "...");
    else if ((int)strlen(s) > max_len)
        out[len] = 0;
    return out;
}

char *compact_symbol(const char *orig, int max_len)
{
    char *buf = strdup(orig);
    if (!buf) return NULL;

    strip_token(buf, "class ");
    strip_token(buf, "struct ");
    strip_token(buf, "enum ");
    strip_token(buf, "noexcept");
    strip_throw_specs(buf);
    strip_trailing_const(buf);
    normalize_lambdas(buf);
    normalize_anonymous_ns(buf);
    tighten_spacing(buf);
    apply_type_abbrev(buf);
    shrink_namespaces(buf);
    shrink_container_templates(buf);
    shrink_iterator_templates(buf);
    shrink_niter_templates(buf);
    shrink_copy_move_templates(buf);
    shrink_function_handler(buf);
    shrink_insert_templates(buf);
    shrink_templates(buf, 32);
    if ((int)strlen(buf) > max_len)
        shrink_params(buf, true, 32);
    if ((int)strlen(buf) > max_len) {
        if (strstr(buf, ",...)") || strstr(buf, "(...)") || strstr(buf, "fn<")) {
            // Params already summarized; avoid dropping them completely.
            head_tail_ellipsis(buf, max_len);
        } else {
            shrink_params(buf, false, 16);
        }
    }
    if ((int)strlen(buf) > max_len)
        head_tail_ellipsis(buf, max_len);

    return buf;
}

