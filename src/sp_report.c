/*
 * sp_report.c - turns a detected problem into a readable diagnosis:
 * what happened, where, which block, who owned it, its life story, and how
 * to fix it.  Also writes one JSON line per issue when SENTINEL_LOG is set.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sp_internal.h"

bool sp__color(void)
{
    static int color = -1;
    if (color < 0) color = isatty(fileno(stderr)) && getenv("NO_COLOR") == NULL;
    return color == 1;
}

#define COL(code) (sp__color() ? (code) : "")
#define RESET COL("\x1b[0m")
#define BOLD  COL("\x1b[1m")
#define DIM   COL("\x1b[2m")

const char *sp__site_str(sp_site at, char *buf, size_t n)
{
    const char *file = at.file ? at.file : "?";
    const char *slash = strrchr(file, '/');
    if (slash) file = slash + 1;
    if (at.line > 0)
        snprintf(buf, n, "%s:%d in %s()", file, at.line, at.func ? at.func : "?");
    else
        snprintf(buf, n, "%s", file);
    return buf;
}

void sp__fmt_bytes(size_t n, char *buf, size_t len)
{
    if (n < 1024) snprintf(buf, len, "%zu bytes", n);
    else if (n < (size_t)1024 * 1024) snprintf(buf, len, "%.1f KB", (double)n / 1024.0);
    else snprintf(buf, len, "%.1f MB", (double)n / (1024.0 * 1024.0));
}

const char *sp__event_name(sp_event_kind kind)
{
    static const char *names[] = {
        "malloc", "make", "free", "move", "share", "drop", "to_shared", "to_unique",
        "release", "adopt", "weak", "lock", "borrow", "unborrow", "realloc",
        "BLOCKED", "deferred", "auto"
    };
    return (unsigned)kind < sizeof names / sizeof names[0] ? names[kind] : "?";
}

/* "&admission" -> "admission",  "&w->owner" -> "w->owner" */
const char *sp__var_name(const char *expr, char *buf, size_t n)
{
    if (expr == NULL) return "";
    while (*expr == '&' || *expr == ' ' || *expr == '(') expr++;
    snprintf(buf, n, "%s", expr);
    size_t len = strlen(buf);
    while (len && (buf[len - 1] == ')' || buf[len - 1] == ' ')) buf[--len] = '\0';
    return buf;
}

static const char *severity(sp_issue kind)
{
    switch (kind) {
    case SP_ISSUE_LEAK:
    case SP_ISSUE_CYCLE:
    case SP_ISSUE_OWNERSHIP:
    case SP_ISSUE_BORROW:      return "warning";
    case SP_ISSUE_ALLOC_FAIL:  return "note";
    default:                   return "error";
    }
}

static const char *title(sp_issue kind)
{
    static const char *t[SP_ISSUE_COUNT_] = {
        "DOUBLE FREE", "INVALID FREE", "FREE OF INTERIOR POINTER", "BUFFER OVERFLOW",
        "USE AFTER FREE", "MEMORY LEAK", "REFERENCE CYCLE", "OWNERSHIP CONFLICT",
        "SMART POINTER MISUSE", "BORROW CONFLICT", "ALLOCATION FAILED"
    };
    return t[kind];
}

static const char *sev_color(sp_issue kind)
{
    const char *s = severity(kind);
    if (s[0] == 'e') return COL("\x1b[1;31m");
    if (s[0] == 'w') return COL("\x1b[1;33m");
    return COL("\x1b[1;36m");
}

/* Print "│ label    text" wrapping text at ~78 columns. */
static void field(FILE *out, const char *label, const char *text)
{
    if (text == NULL || *text == '\0') return;
    const int width = 68;
    const char *p = text;
    bool first = true;
    while (*p) {
        int take = (int)strlen(p);
        if (take > width) {
            take = width;
            while (take > 0 && p[take] != ' ') take--;
            if (take == 0) take = width;
        }
        fprintf(out, "%s│%s %s%-9s%s %.*s\n", DIM, RESET, BOLD, first ? label : "", RESET, take, p);
        p += take;
        while (*p == ' ') p++;
        first = false;
    }
}

static void owner_line(const sp_record *r, char *buf, size_t n)
{
    size_t used = (size_t)snprintf(buf, n, "%s", sp_kind_name(r->kind));
    if (r->kind == SP_SHARED && used < n)
        used += (size_t)snprintf(buf + used, n - used, " (%zu owner%s)", r->strong, r->strong == 1 ? "" : "s");
    if (r->weak && used < n)
        used += (size_t)snprintf(buf + used, n - used, ", %zu weak observer%s", r->weak, r->weak == 1 ? "" : "s");
    if (r->borrows && used < n)
        used += (size_t)snprintf(buf + used, n - used, ", %zu active borrow%s", r->borrows, r->borrows == 1 ? "" : "s");
    if (r->label[0] && used < n)
        snprintf(buf + used, n - used, " - named \"%s\"", r->label);
}

static void history(FILE *out, const sp_record *r)
{
    size_t start = r->nevents > SP_HISTORY ? r->nevents - SP_HISTORY : 0;
    char line[200], site[120], var[96];
    if (start > 0) {
        snprintf(line, sizeof line, "... %zu earlier event(s)", start);
        field(out, "timeline", line);
    }
    for (size_t k = start; k < r->nevents; ++k) {
        const sp_event *e = &r->history[k % SP_HISTORY];
        {
            const char *file = e->at.file ? e->at.file : "?";
            const char *slash = strrchr(file, '/');
            if (slash) file = slash + 1;
            if (e->at.line > 0) snprintf(site, sizeof site, "%s:%d %s()", file, e->at.line, e->at.func ? e->at.func : "?");
            else snprintf(site, sizeof site, "%s", file);
        }
        const char *x = e->expr ? sp__var_name(e->expr, var, sizeof var) : "";
        char extra[64] = "";
        if (e->kind == EV_SHARE || e->kind == EV_LOCK || (e->kind == EV_DROP && e->aux > 0))
            snprintf(extra, sizeof extra, " [%zu owner%s]", e->aux, e->aux == 1 ? "" : "s");
        else if (e->kind == EV_REALLOC)
            snprintf(extra, sizeof extra, " [from #%zu]", e->aux);
        snprintf(line, sizeof line, "%2zu. %-9s %s%s%s%s", k + 1, sp__event_name(e->kind), site,
                 *x ? " `" : "", x, *x ? "`" : "");
        size_t len = strlen(line);
        snprintf(line + len, sizeof line - len, "%s", extra);
        fprintf(out, "%s│%s %s%-9s%s %s\n", DIM, RESET, BOLD, (k == start && start == 0) ? "timeline" : "", RESET, line);
    }
}

static void block_fields(FILE *out, const sp_record *r)
{
    char buf[256], site[120], size[32];
    if (r->foreign) snprintf(size, sizeof size, "unknown size");
    else sp__fmt_bytes(r->size, size, sizeof size);
    snprintf(buf, sizeof buf, "#%llu, %s%s%s", (unsigned long long)r->id, size,
             r->type ? ", type " : "", r->type ? r->type : "");
    field(out, "block", buf);
    owner_line(r, buf, sizeof buf);
    field(out, "owner", buf);
    field(out, "created", sp__site_str(r->alloc_at, site, sizeof site));
    if (r->state == ST_FREED || r->state == ST_EXPIRED)
        field(out, "released", sp__site_str(r->free_at, site, sizeof site));
    history(out, r);
}

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; s && *s; ++s) {
        if (*s == '"' || *s == '\\') { fputc('\\', f); fputc(*s, f); }
        else if ((unsigned char)*s < 0x20) fprintf(f, "\\u%04x", *s);
        else fputc(*s, f);
    }
    fputc('"', f);
}

static void log_json(sp_issue kind, sp_site at, const sp_record *r, const char *what,
                     const char *detail, const char *fix)
{
    FILE *f = sp__log_file();
    if (f == NULL) return;
    fprintf(f, "{\"issue\":");
    json_str(f, sp_issue_name(kind));
    fprintf(f, ",\"severity\":");
    json_str(f, severity(kind));
    fprintf(f, ",\"file\":");
    json_str(f, at.file);
    fprintf(f, ",\"line\":%d,\"function\":", at.line);
    json_str(f, at.func);
    if (r) {
        fprintf(f, ",\"block\":%llu,\"size\":%zu,\"kind\":", (unsigned long long)r->id, r->size);
        json_str(f, sp_kind_name(r->kind));
        fprintf(f, ",\"created_file\":");
        json_str(f, r->alloc_at.file);
        fprintf(f, ",\"created_line\":%d", r->alloc_at.line);
    }
    fprintf(f, ",\"what\":");
    json_str(f, what);
    fprintf(f, ",\"detail\":");
    json_str(f, detail);
    fprintf(f, ",\"fix\":");
    json_str(f, fix);
    fprintf(f, "}\n");
    fflush(f);
}

void sp__report(sp_issue kind, sp_site at, const char *call, const sp_record *r,
                const char *what, const char *detail, const char *fix, const char *action)
{
    size_t number = sp__count_issue(kind);
    log_json(kind, at, r, what, detail, fix);

    sp_mode mode = sp__mode();
    if (mode == SP_MODE_SILENT || mode == SP_MODE_SUMMARY) return;

    FILE *out = stderr;
    char site[160], where[256];
    sp__site_str(at, site, sizeof site);
    if (call && *call) snprintf(where, sizeof where, "%s  ->  %s", site, call);
    else snprintf(where, sizeof where, "%s", site);

    char tag[48];
    if (kind == SP_ISSUE_ALLOC_FAIL) snprintf(tag, sizeof tag, "note");
    else snprintf(tag, sizeof tag, "%s #%zu", severity(kind), number);
    fprintf(out, "\n%s┌─ SENTINEL ─%s %s%s%s %s(%s)%s\n", DIM, RESET, sev_color(kind), title(kind),
            RESET, DIM, tag, RESET);
    field(out, "what", what);
    field(out, "where", where);
    if (r) block_fields(out, r);
    field(out, "detail", detail);
    if (fix) {
        fprintf(out, "%s│%s %s%-9s%s ", DIM, RESET, COL("\x1b[1;32m"), "fix", RESET);
        /* reuse the wrapper but keep the green label on the first line */
        char buf[1024];
        snprintf(buf, sizeof buf, "%s", fix);
        const int width = 68;
        const char *p = buf;
        bool first = true;
        while (*p) {
            int take = (int)strlen(p);
            if (take > width) {
                take = width;
                while (take > 0 && p[take] != ' ') take--;
                if (take == 0) take = width;
            }
            if (!first) fprintf(out, "%s│%s %-9s ", DIM, RESET, "");
            fprintf(out, "%.*s\n", take, p);
            p += take;
            while (*p == ' ') p++;
            first = false;
        }
    }
    fprintf(out, "%s└─ %s%s\n", DIM, action ? action : "", RESET);
    fflush(out);

    if (mode == SP_MODE_ABORT && kind != SP_ISSUE_ALLOC_FAIL) {
        fprintf(out, "sentinel: aborting (SENTINEL=abort)\n");
        abort();
    }
}

void sp__print_block(FILE *out, const sp_record *r)
{
    static const char *states[] = { "live", "being destroyed", "freed (in quarantine)", "expired" };
    char buf[64];
    snprintf(buf, sizeof buf, "%s", states[r->state]);
    fprintf(out, "\n%s┌─ SENTINEL ─%s %sBLOCK #%llu%s\n", DIM, RESET, BOLD, (unsigned long long)r->id, RESET);
    field(out, "state", buf);
    block_fields(out, r);
    fprintf(out, "%s└─%s\n", DIM, RESET);
}
