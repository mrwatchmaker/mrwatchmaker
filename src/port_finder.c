/*
 * MrWatchmaker 포트 찾기
 * COM 포트를 검색하여 port.conf에 자동 저장
 * - ST3215 서보가 연결된 포트 자동 감지 (Ping)
 * - 감지 실패 시 사용자가 목록에서 선택
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAUDRATE 1000000
#define PING_ID   1

static uint8_t checksum(uint8_t *data, int len) {
    int sum = 0;
    for (int i = 0; i < len; i++) sum += data[i];
    return (~sum) & 0xFF;
}

/* 포트에 Ping 전송, 응답 있으면 1 */
static int try_ping(HANDLE h) {
    uint8_t chk_data[] = { PING_ID, 0x02, 0x01 };
    uint8_t ping[] = { 0xFF, 0xFF, PING_ID, 0x02, 0x01, 0 };
    ping[5] = checksum(chk_data, 3);
    uint8_t buf[16];
    DWORD written, read;

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    if (!WriteFile(h, ping, 6, &written, NULL) || written != 6) return 0;

    Sleep(30);
    if (!ReadFile(h, buf, sizeof(buf), &read, NULL) || read < 6) return 0;

    /* Status packet: 0xFF 0xFF ID LEN 0x00 CHK */
    if (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == PING_ID && buf[4] == 0x00)
        return 1;
    return 0;
}

/* 포트 열고 1Mbps 설정 */
static HANDLE open_port(const char *name) {
    HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                          OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return NULL; }
    dcb.BaudRate = BAUDRATE;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    if (!SetCommState(h, &dcb)) { CloseHandle(h); return NULL; }

    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout = 30;
    to.ReadTotalTimeoutConstant = 50;
    to.ReadTotalTimeoutMultiplier = 10;
    to.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(h, &to);
    return h;
}

/* exe 경로에서 port.conf 경로 얻기 */
static void get_port_conf_path(char *out, size_t size) {
    char exe[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0) {
        strcpy(out, "port.conf");
        return;
    }
    char *slash = strrchr(exe, '\\');
    if (slash) {
        slash[1] = '\0';
        snprintf(out, (int)size, "%sport.conf", exe);
    } else {
        strcpy(out, "port.conf");
    }
}

/* port.conf에 저장 */
static int save_port(const char *port) {
    char path[MAX_PATH + 16];
    get_port_conf_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "%s\n", port);
    fclose(f);
    return 1;
}

int main(void) {
    char path[MAX_PATH + 16];
    get_port_conf_path(path, sizeof(path));

    printf("\n  === MrWatchmaker COM Port Finder ===\n\n");
    printf("  ST3215 서보 모터가 연결된 COM 포트를 찾습니다.\n\n");

    /* COM1 ~ COM256 중 존재하는 포트 수집 */
    char *ports[64];
    int nports = 0;
    for (int i = 1; i <= 256 && nports < 64; i++) {
        char name[16];
        snprintf(name, sizeof(name), "COM%d", i);
        HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            ports[nports] = (char*)malloc(16);
            strcpy(ports[nports], name);
            nports++;
        }
    }

    if (nports == 0) {
        printf("  사용 가능한 COM 포트가 없습니다.\n");
        printf("  USB-시리얼 케이블이 연결되어 있는지 확인하세요.\n\n");
        return 1;
    }

    printf("  발견된 포트: ");
    for (int i = 0; i < nports; i++) printf("%s ", ports[i]);
    printf("\n\n");

    /* 자동 감지: 각 포트에 Ping 시도 */
    printf("  서보 모터 검색 중...\n\n");
    for (int i = 0; i < nports; i++) {
        HANDLE h = open_port(ports[i]);
        if (h) {
            if (try_ping(h)) {
                CloseHandle(h);
                if (save_port(ports[i])) {
                    printf("  [자동 감지] %s 에 서보 모터가 연결되어 있습니다.\n", ports[i]);
                    printf("  port.conf 에 저장했습니다.\n\n");
                    for (int j = 0; j < nports; j++) free(ports[j]);
                    return 0;
                }
            }
            CloseHandle(h);
        }
    }

    /* 자동 감지 실패 - 사용자 선택 */
    printf("  서보 모터를 자동으로 찾지 못했습니다.\n");
    printf("  (케이블 연결, 전원, 드라이버 확인 후 다시 시도하세요)\n\n");
    printf("  사용할 COM 포트를 선택하세요:\n\n");
    for (int i = 0; i < nports; i++)
        printf("    %2d. %s\n", i + 1, ports[i]);
    printf("\n  번호 입력 (1-%d): ", nports);

    int sel = 0;
    if (scanf("%d", &sel) != 1 || sel < 1 || sel > nports) {
        printf("  잘못된 입력입니다.\n");
        for (int i = 0; i < nports; i++) free(ports[i]);
        return 1;
    }

    const char *chosen = ports[sel - 1];
    if (save_port(chosen)) {
        printf("\n  %s 를 port.conf 에 저장했습니다.\n", chosen);
        printf("  mrwatchmaker.exe 를 실행하세요.\n\n");
    } else {
        printf("\n  저장 실패. 수동으로 port.conf 에 %s 를 입력하세요.\n\n", chosen);
    }

    for (int i = 0; i < nports; i++) free(ports[i]);
    return 0;
}
