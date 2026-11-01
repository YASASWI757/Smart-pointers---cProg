/*
 * test_exit_report.c - checks the at-exit analysis (leaks, indirect leaks,
 * cycles) by running itself as a child with SENTINEL_LOG and reading the
 * JSON lines it writes.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "smartptr.h"
#include "check.h"

typedef struct Node {
    struct Node *next;
    char *payload;
} Node;

static int child(void)
{
    for (int i = 0; i < 3; ++i) (void)malloc(16);       /* 1 group: 3 direct leaks */

    Node *holder = malloc(sizeof *holder);              /* direct leak ...           */
    holder->payload = strdup("inside");                 /* ... and an indirect leak  */
    holder->next = NULL;

    Node *a = sp_make_shared(Node);                     /* a 2-node cycle */
    Node *b = sp_make_shared(Node);
    a->next = sp_share(b);
    b->next = sp_share(a);
    sp_drop(&a);
    sp_drop(&b);

    char *fine = malloc(8);                             /* not a leak */
    free(fine);
    return 0;
}

static size_t count(const char *text, const char *needle)
{
    size_t n = 0;
    for (const char *p = strstr(text, needle); p; p = strstr(p + 1, needle)) n++;
    return n;
}

static void test_exit_analysis(const char *self)
{
    const char *log = "build/exit_report.jsonl";
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        setenv("SENTINEL", "silent", 1);
        setenv("SENTINEL_LOG", log, 1);
        execl(self, self, "child", (char *)NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    FILE *f = fopen(log, "r");
    CHECK(f != NULL);
    static char text[16384];
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);

    CHECK(count(text, "\"issue\":\"memory leak\"") == 3);    /* loop site, holder, payload */
    CHECK(count(text, "\"issue\":\"reference cycle\"") == 1);
    CHECK(count(text, "3 direct") == 1);
    CHECK(count(text, "0 direct (nothing else points to them) + 1 indirect") == 1);
    CHECK(count(text, "\"summary\"") == 1);
}

int main(int argc, char **argv)
{
    if (argc > 1) return child();
    printf("test_exit_report\n");
    test_exit_analysis(argv[0]);
    check_passed++;
    printf("  ok  test_exit_analysis\n  %d passed\n", check_passed);
    return 0;
}
