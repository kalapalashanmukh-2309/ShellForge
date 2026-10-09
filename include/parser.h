#ifndef PARSER_H
#define PARSER_H
#include "shell.h"
int parse_line(const char *line, Pipeline *pipeline);
void free_pipeline(Pipeline *pipeline);
#endif
