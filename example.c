#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

int main() {
    int pipefd[2];
    pid_t pid;
    char buffer[1024];

    // 파이프 생성
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // fork 호출
    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) { // 자식 프로세스
        // 파이프의 쓰기 끝을 닫습니다.
        close(pipefd[1]);

        // 파이프의 읽기 끝을 stdin으로 복제합니다.
        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }

        // 파이프의 읽기 끝을 닫습니다. (stdin에 복제되었으므로 더 이상 필요 없음)
        close(pipefd[0]);

        // 자식 프로세스에서 입력을 읽어 처리
        char line[1024];
        while (fgets(line, sizeof(line), stdin) != NULL) {
            printf("Child process received: %s", line);
        }

        exit(EXIT_SUCCESS);

    } else { // 부모 프로세스
        // 파이프의 읽기 끝을 닫습니다.
        close(pipefd[0]);

        // 사용자 입력을 받아 파이프로 보냅니다.
        printf("Enter text (type 'exit' to quit):\n");
        while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            // 'exit'가 입력되면 종료
            if (strncmp(buffer, "exit", 4) == 0) {
                break;
            }
            write(pipefd[1], buffer, strlen(buffer));
        }

        // 파이프의 쓰기 끝을 닫습니다.
        close(pipefd[1]);

        // 자식 프로세스가 끝날 때까지 기다립니다.
        wait(NULL);
    }

    return 0;
}