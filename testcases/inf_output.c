#include <stdio.h>
#include <unistd.h>

int main() {
    while (1) {
        sleep(10);
        printf("Hi\n");
    }
    return 0;
}