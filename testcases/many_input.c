#include <stdio.h>

int main() {
  while (1) {
    int N;
    scanf("%d", &N);
    if (N == 0)
      break;
    printf("%d * 2 = %d\n", N, N * 2);
  }
  return 0;
}