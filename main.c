#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <wait.h>

#define MAX_PROCESS 16
#define BUF_SIZE 1024

struct process_running {
    pid_t pid;
    int from_child_pipe[2];
    int to_child_pipe[2];
    int is_running;
};

struct process_running ps[MAX_PROCESS];
extern char **environ;

int set_FD_CLOEXEC(int fd) {
    // FD_CLOEXEC 플래그 설정
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        perror("fcntl - F_GETFD");
        close(fd);
        return EXIT_FAILURE;
    }

    flags |= FD_CLOEXEC;

    if (fcntl(fd, F_SETFD, flags) == -1) {
        perror("fcntl - F_SETFD");
        close(fd);
        return EXIT_FAILURE;
    }
}

pid_t check_pid_alive(pid_t pid) {
    printf("CHECK PID %d ... ", pid);
    if (kill(pid, 0) == 0) {
        printf("GOOD\n");
        return pid;
    }
    return 0;
}

int cleanup_child_process(struct process_running *p_info) {
    if (p_info->is_running == 0) {
        return 0;
    }
    pid_t ret = waitpid(p_info->pid, NULL, WNOHANG);
    if (ret == 0) {
        return 0;
    }
    close(p_info->to_child_pipe[1]);
    close(p_info->from_child_pipe[0]);

    p_info->is_running = 0;
    return 1;
}

int handle_run(char *path_to_source_code, char *command_line_args[]) {
    int pidx = 0;    
    /* 일단 임시로 gcc + output 또한 임시로 */
    char *compile_args[] = {"/usr/bin/gcc", path_to_source_code, "-o", command_line_args[0], (char *)NULL};
    for (pidx = 0; pidx < MAX_PROCESS && ps[pidx].is_running; pidx++)
        ;
    if (pidx == MAX_PROCESS) {
        printf("NO MORE PRECESS\n");
        return -1;
    }

    ps[pidx] = (struct process_running){
        .is_running = 1,
    };
    // 파이프 생성
    if (pipe(ps[pidx].from_child_pipe) == -1) {
        perror("pipe");
        return -1;
    }
    if (pipe(ps[pidx].to_child_pipe) == -1) {
        perror("pipe");
        return -1;
    }

    ps[pidx].pid = fork();
    if (ps[pidx].pid == -1) {
        perror("fork");
        return -1;
    } else if (ps[pidx].pid == 0) {

        /* 표준 입출력의 리다이렉션 */
        close(ps[pidx].from_child_pipe[0]);
        dup2(ps[pidx].from_child_pipe[1], STDOUT_FILENO);
        dup2(ps[pidx].from_child_pipe[1], STDERR_FILENO);
        close(ps[pidx].from_child_pipe[1]);

        close(ps[pidx].to_child_pipe[1]);
        dup2(ps[pidx].to_child_pipe[0], STDIN_FILENO);
        close(ps[pidx].to_child_pipe[0]);

        /* 목표 프로세스 실행 */        
        pid_t compiler_pid;
        int compiler_status;        
        posix_spawn(&compiler_pid, compile_args[0], NULL, NULL, compile_args, environ);
        perror("posix_spawn");
        waitpid(compiler_pid, &compiler_status, 0);
        if (!WIFEXITED(compiler_status)) {
          printf("Compile failed (unknown reason) \n");
          exit(EXIT_FAILURE);
        }         
        if (WEXITSTATUS(compiler_status) != 0) {
          printf("Compile failed (compile error) \n");
          exit(EXIT_FAILURE);
        }
        execv(command_line_args[0], command_line_args);

        /* execv failed */
        perror("exec");
        exit(EXIT_FAILURE);
    }
    close(ps[pidx].to_child_pipe[0]);
    close(ps[pidx].from_child_pipe[1]);
    ps[pidx].to_child_pipe[0] = -1;
    ps[pidx].from_child_pipe[1] = -1;

    /* 다른 자식 프로세스에서는 해당 파일 기술자 에 접근하지 못하게 하기 위함 */
    set_FD_CLOEXEC(ps[pidx].to_child_pipe[1]);
    set_FD_CLOEXEC(ps[pidx].from_child_pipe[0]);

    int inpipe_flags = fcntl(ps[pidx].from_child_pipe[0], F_GETFL);
    if (fcntl(ps[pidx].from_child_pipe[0], F_SETFL, inpipe_flags | O_NONBLOCK) ==
        -1) {
        perror("fcntl");
        return -1;
    }
    return pidx;
}

void show_process_list() {
    for (int i = 0; i < MAX_PROCESS; i++) {        
        printf("**[%5d] PID: %7d\tSTATUS: %s\n", i, ps[i].pid,
               ps[i].is_running ? "\033[32mACTIVE\033[0m"
                                : "\033[31mINACTIVE\033[0m");
    }
}

int pass_input_to_child(int pidx) {
    if (!check_pid_alive(ps[pidx].pid)) {
        printf("CANNOT ACCESS TO PROCESS\n");
        return -1;
    }

    char buf[1024];
    char *n;
    printf("Enter text (type 'exit' to quit):\n");
    while ((n = fgets(buf, sizeof(buf), stdin)) != NULL) {
        // 'exit'가 입력되면 종료
        if (strncmp(buf, "exit", 4) == 0) {
            break;
        }
        ssize_t n;
        n = write(ps[pidx].to_child_pipe[1], buf, strlen(buf));
        if (n < 0) {
            printf("PROCESS CANNOT GET INPUT\n");
            break;
        }
    }
    if (n < 0) {
        perror("write");
    }
}

int get_output_from_child(int pidx) {
    printf("** READ\n");
    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(ps[pidx].from_child_pipe[0], buf, BUF_SIZE - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    printf("\n");

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("That's all\n");
            return 0;
        }
        printf("CANNOT READ ANYMORE (in any reason)\n");
        printf("Check is PROCESS DONE ... ");
        if (cleanup_child_process(&ps[pidx])) {
            printf("EXITED\n");
            return 1;
        } else {
            printf("NOT DONE\n");
            return -1;
        }
    }
}

int main() {
  int temp = 0;
    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < MAX_PROCESS; i++) {
        *ps[i].to_child_pipe = (int[2]){-1, -1};
        *ps[i].from_child_pipe = (int[2]){-1, -1};
        ps[i].pid = 0;
        ps[i].is_running = 0;
    }    

    while (1) {
        printf("1. new process\n2. process list\n3. pass input\n4. check "
               "output\n> ");
        int order;
        int pidx;
        scanf("%d", &order);

        if (order == 1) {
            char path_to_source_code[128];        
            char bin_file[64];

            printf("source code path = ");
            scanf("%s", path_to_source_code);
            
            sprintf(bin_file, "./temp/%d.out", temp);
            char *cli_args[] = {bin_file, (char *)NULL};
            if (handle_run(path_to_source_code, cli_args) >= 0) {
                printf("SUCCESSFULLY RUN\n");
            }
        } else if (order == 2) {
            show_process_list();
        } else if (order == 3) {
            printf("PROCESS = ");
            scanf("%d", &pidx);
            pass_input_to_child(pidx);
        } else if (order == 4) {
            printf("PROCESS = ");
            scanf("%d", &pidx);
            get_output_from_child(pidx);
        }
    }

    return 0;
}