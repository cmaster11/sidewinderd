//
// Created by cmaster11 on 24.9.2016.
//

#include "command_executor.hpp"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <cstdlib>

// Ref: http://stackoverflow.com/questions/5237482/how-do-i-execute-external-program-within-c-code-in-linux-with-arguments
int CommandExecutor::exec_prog(const char **argv) {
        pid_t   my_pid;

        if (0 == (my_pid = fork())) {

            if (-1 == execv(argv[0], (char **)argv)) {
                perror("child process execv failed");
                return -1;
            }
        }
    return 0;
}
