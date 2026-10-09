#include "input.h"
#include "shell.h"
#include "signals.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

char *read_line(void)
{
    size_t size = 128, used = 0;
    char *line = malloc(size);
    if (!line) { perror("malloc"); return NULL; }
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (used == MAX_LINE) {
            while ((ch = getchar()) != EOF && ch != '\n') {}
            free(line);
            fprintf(stderr, "shellforge: input exceeds %d bytes\n", MAX_LINE);
            errno = E2BIG;
            return NULL;
        }
        if (used + 1 == size) {
            size *= 2;
            char *grown = realloc(line, size);
            if (!grown) { free(line); perror("realloc"); return NULL; }
            line = grown;
        }
        line[used++] = (char)ch;
    }
    if (ferror(stdin)) {
        int saved = errno;
        clearerr(stdin);
        free(line);
        errno = saved;
        return NULL;
    }
    if (ch == EOF && used == 0) { free(line); errno = 0; return NULL; }
    line[used] = '\0';
    return line;
}
