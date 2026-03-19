#include <stdio.h>
#include <string.h>

const char *s = "  평균 레이트가 우수한 범위에 있습니다.\n  시계의 전반적인 조율 상태가 양호합니다.\n";

int main() {
    for (int i = 0; s[i] != '\0'; i++) {
        printf("%02x ", (unsigned char)s[i]);
    }
    printf("\n");
    return 0;
}
