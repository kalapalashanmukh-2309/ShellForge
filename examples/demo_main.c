#include <string.h>
void demonstrate_process_creation(void);
void demonstrate_producer_consumer(void);
void demonstrate_ls_grep_pipe(void);
int main(int argc, char **argv)
{
    (void)argc;
    if (strstr(argv[0], "process")) demonstrate_process_creation();
    else if (!strcmp(argv[0], "pipe") || strstr(argv[0], "pipe-demo")) demonstrate_producer_consumer();
    else demonstrate_ls_grep_pipe();
    return 0;
}
