#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_broken_pipe PROGRAM [ARGS...]\n");
        return 125;
    }
    int descriptors[2];
    if (pipe(descriptors) != 0) {
        perror("pipe");
        return 125;
    }
    if (close(descriptors[0]) != 0) {
        perror("close read end");
        close(descriptors[1]);
        return 125;
    }
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        close(descriptors[1]);
        return 125;
    }
    if (child == 0) {
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) {
            perror("dup2");
            _exit(125);
        }
        close(descriptors[1]);
        execv(argv[1], &argv[1]);
        perror("execv");
        _exit(errno == ENOENT ? 127 : 126);
    }
    close(descriptors[1]);
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return 125;
        }
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 125;
}

