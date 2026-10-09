#include "parser.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { END, WORD, PIPE, BACKGROUND, INPUT, OUTPUT, APPEND, ERROR_OUT, BAD } Kind;
typedef struct { Kind kind; char *word; } Token;

static Token next_token(const char **cursor)
{
    const char *p = *cursor;
    while (isspace((unsigned char)*p)) p++;
    Token token = {END, NULL};
    if (!*p || *p == '#') { *cursor = p + strlen(p); return token; }
    if (p[0] == '2' && p[1] == '>') { token.kind = ERROR_OUT; p += 2; }
    else if (*p == '>') { token.kind = p[1] == '>' ? APPEND : OUTPUT; p += token.kind == APPEND ? 2 : 1; }
    else if (*p == '<') { token.kind = INPUT; p++; }
    else if (*p == '|') { token.kind = PIPE; p++; }
    else if (*p == '&') { token.kind = BACKGROUND; p++; }
    else if (*p == ';' || *p == '`') { token.kind = BAD; p++; }
    else {
        token.kind = WORD;
        token.word = malloc(strlen(p) + 1);
        if (!token.word) { token.kind = BAD; return token; }
        size_t n = 0;
        char quote = 0;
        while (*p) {
            if (!quote && (isspace((unsigned char)*p) || strchr("|&<>;", *p))) break;
            if (!quote && (*p == '\'' || *p == '"')) { quote = *p++; continue; }
            if (quote && *p == quote) { quote = 0; p++; continue; }
            if (*p == '\\' && quote != '\'') {
                p++;
                if (!*p) { token.kind = BAD; break; }
                if (quote == '"' && !strchr("\\\"$`", *p)) token.word[n++] = '\\';
            } else if (!quote && *p == '`') { token.kind = BAD; break; }
            token.word[n++] = *p++;
        }
        token.word[n] = '\0';
        if (quote) token.kind = BAD;
    }
    *cursor = p;
    return token;
}

void free_pipeline(Pipeline *pipeline)
{
    for (size_t i = 0; i < MAX_COMMANDS; i++) {
        Command *c = &pipeline->commands[i];
        for (size_t j = 0; j < c->argc; j++) free(c->argv[j]);
        for (size_t j = 0; j < c->redirect_count; j++) free(c->redirects[j].path);
    }
    memset(pipeline, 0, sizeof(*pipeline));
}

int parse_line(const char *line, Pipeline *pipeline)
{
    memset(pipeline, 0, sizeof(*pipeline));
    if (strlen(line) > MAX_LINE) { fprintf(stderr, "shellforge: input exceeds %d bytes\n", MAX_LINE); return -1; }
    const char *cursor = line;
    pipeline->count = 1;
    for (;;) {
        Token t = next_token(&cursor);
        Command *c = &pipeline->commands[pipeline->count - 1];
        if (t.kind == END) {
            if (!c->argc && (pipeline->count > 1 || c->redirect_count)) goto error;
            if (!c->argc) pipeline->count = 0;
            return 0;
        }
        if (t.kind == WORD) {
            if (c->argc == MAX_ARGS) { free(t.word); goto error; }
            c->argv[c->argc++] = t.word;
        } else if (t.kind >= INPUT && t.kind <= ERROR_OUT) {
            Token filename = next_token(&cursor);
            if (filename.kind != WORD || c->redirect_count == MAX_REDIRECTS) { free(filename.word); goto error; }
            Redirect *r = &c->redirects[c->redirect_count++];
            r->target = t.kind == INPUT ? 0 : t.kind == ERROR_OUT ? 2 : 1;
            r->flags = t.kind == INPUT ? O_RDONLY : O_WRONLY | O_CREAT | (t.kind == APPEND ? O_APPEND : O_TRUNC);
            r->path = filename.word;
        } else if (t.kind == PIPE) {
            if (!c->argc || pipeline->count == MAX_COMMANDS) goto error;
            pipeline->count++;
        } else if (t.kind == BACKGROUND) {
            Token tail = next_token(&cursor);
            bool valid = tail.kind == END && c->argc;
            free(tail.word);
            if (!valid) goto error;
            pipeline->background = true;
            return 0;
        } else { free(t.word); goto error; }
    }
error:
    fprintf(stderr, "shellforge: invalid syntax, missing filename, or command limit exceeded\n");
    free_pipeline(pipeline);
    return -1;
}
