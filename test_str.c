#include <stdio.h>
#include <string.h>

typedef struct {
    const char *key;
    const char *val;
} dict_entry_t;

static dict_entry_t dict_en[] = {
    {"  평균 레이트가 우수한 범위에 있습니다.\n  시계의 전반적인 조율 상태가 양호합니다.\n", "  The average rate is in an excellent range.\n  The overall tuning of the watch is good.\n"},
    {NULL, NULL}
};

int main() {
    const char *key = "  평균 레이트가 우수한 범위에 있습니다.\n  시계의 전반적인 조율 상태가 양호합니다.\n";
    printf("Key length: %zu\n", strlen(key));
    printf("Dict key length: %zu\n", strlen(dict_en[0].key));
    
    if (strcmp(key, dict_en[0].key) == 0) {
        printf("Matches!\n");
    } else {
        printf("Does NOT match!\n");
        for (int i = 0; i < strlen(key) && i < strlen(dict_en[0].key); i++) {
            if (key[i] != dict_en[0].key[i]) {
                printf("Mismatch at %d: %x vs %x\n", i, (unsigned char)key[i], (unsigned char)dict_en[0].key[i]);
                break;
            }
        }
    }
    return 0;
}
