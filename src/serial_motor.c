#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

static HANDLE hSerial = INVALID_HANDLE_VALUE;
static char port_buf[16] = "COM5";  /* 기본값 */

/* exe와 같은 폴더의 port.conf에서 COM 포트 읽기. 없으면 COM5 */
const char *motor_get_port(void) {
	char exe_path[MAX_PATH];
	DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
	if (len > 0 && len < sizeof(exe_path)) {
		char *slash = strrchr(exe_path, '\\');
		if (slash) {
			slash[1] = '\0';
			strcat(exe_path, "port.conf");
			FILE *f = fopen(exe_path, "r");
			if (f) {
				char line[32];
				if (fgets(line, sizeof(line), f)) {
					char *p = line;
					while (*p && *p != 'C') p++;
					if (p[0]=='C' && p[1]=='O' && p[2]=='M') {
						int n = 0;
						sscanf(p+3, "%d", &n);
						if (n >= 1 && n <= 99) {
							fclose(f);
							snprintf(port_buf, sizeof(port_buf), "COM%d", n);
							return port_buf;
						}
					}
				}
				fclose(f);
			}
		}
	}
	strcpy(port_buf, "COM5");
	return port_buf;
}

int motor_init(const char *port_name) {
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
    }
    
    hSerial = CreateFileA(port_name,
                          GENERIC_READ | GENERIC_WRITE,
                          0,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
                          
    if (hSerial == INVALID_HANDLE_VALUE) {
        printf("Error opening serial port %s\n", port_name);
        return 0;
    }
    
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        printf("Error getting state\n");
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        return 0;
    }
    
    dcbSerialParams.BaudRate = 1000000; // 1 Mbps for ST3215
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity   = NOPARITY;
    
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        printf("Error setting state\n");
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        return 0;
    }
    
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = 50;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    
    SetCommTimeouts(hSerial, &timeouts);
    printf("Successfully opened %s at 1000000 baud\n", port_name);
    return 1;
}

static uint8_t checksum(uint8_t *data, int len) {
    int sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i];
    }
    return (~sum) & 0xFF;
}

/* STS3215 목표 위치: 0~4095만. Face/Arm 안전구간은 output_panel + mrwatchmaker_coords.txt
 * 의 face_lim / arm_lim 에서만 적용한다. 여기서 고정 1935 등으로 자르면 UI와 모터가 어긋난다. */
static int clamp_pos_for_servo(uint8_t servo_id, int position) {
    (void)servo_id;
    if (position < 0) return 0;
    if (position > 4095) return 4095;
    return position;
}

void motor_move(uint8_t servo_id, int position, int time_ms, int speed) {
    if (hSerial == INVALID_HANDLE_VALUE) return;

    position = clamp_pos_for_servo(servo_id, position);
    
    uint8_t pos_l = position & 0xFF;
    uint8_t pos_h = (position >> 8) & 0xFF;
    uint8_t time_l = time_ms & 0xFF;
    uint8_t time_h = (time_ms >> 8) & 0xFF;
    uint8_t speed_l = speed & 0xFF;
    uint8_t speed_h = (speed >> 8) & 0xFF;
    
    uint8_t packet[10];
    packet[0] = servo_id;
    packet[1] = 9; // length
    packet[2] = 0x03; // write
    packet[3] = 0x2A; // address (Goal Position)
    packet[4] = pos_l;
    packet[5] = pos_h;
    packet[6] = time_l;
    packet[7] = time_h;
    packet[8] = speed_l;
    packet[9] = speed_h;
    
    uint8_t chk = checksum(packet, 10);
    
    uint8_t full_packet[13];
    full_packet[0] = 0xFF;
    full_packet[1] = 0xFF;
    for (int i = 0; i < 10; i++) full_packet[2+i] = packet[i];
    full_packet[12] = chk;
    
    DWORD bytes_written;
    WriteFile(hSerial, full_packet, 13, &bytes_written, NULL);
}

/* STS3215: 1바이트 쓰기 (addr 0x28 = Torque Enable 등) */
void motor_write_byte(uint8_t servo_id, uint8_t addr, uint8_t value) {
    if (hSerial == INVALID_HANDLE_VALUE) return;
    uint8_t packet[5];
    packet[0] = servo_id;
    packet[1] = 4;
    packet[2] = 0x03;
    packet[3] = addr;
    packet[4] = value;
    uint8_t chk = checksum(packet, 5);
    uint8_t full[8];
    full[0] = 0xFF;
    full[1] = 0xFF;
    for (int i = 0; i < 5; i++) full[2 + i] = packet[i];
    full[7] = chk;
    DWORD written;
    WriteFile(hSerial, full, 8, &written, NULL);
}

/* STS3215: Present Position 읽기 (addr 0x38, 2바이트). 실패 시 -1 */
int motor_read_present_position(uint8_t servo_id) {
    if (hSerial == INVALID_HANDLE_VALUE) return -1;
    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
    uint8_t packet[5];
    packet[0] = servo_id;
    packet[1] = 4;
    packet[2] = 0x02; /* read */
    packet[3] = 0x38; /* start addr */
    packet[4] = 0x02; /* 2 bytes */
    uint8_t chk = checksum(packet, 5);
    uint8_t full[8];
    full[0] = 0xFF;
    full[1] = 0xFF;
    for (int i = 0; i < 5; i++) full[2 + i] = packet[i];
    full[7] = chk;
    DWORD written;
    WriteFile(hSerial, full, 8, &written, NULL);
    Sleep(50);
    uint8_t buf[16];
    DWORD read_len = 0;
    for (int retry = 0; retry < 5; retry++) {
        if (ReadFile(hSerial, buf, sizeof(buf), &read_len, NULL) && read_len >= 8) break;
        Sleep(20);
        read_len = 0;
    }
    if (read_len < 8) return -1;
    for (DWORD i = 0; i + 8 <= read_len; i++) {
        if (buf[i] == 0xFF && buf[i+1] == 0xFF && buf[i+2] == servo_id) {
            int pos_l = buf[i+5] & 0xFF;
            int pos_h = buf[i+6] & 0xFF;
            int pos = (pos_h << 8) | pos_l;
            if (pos > 4095 && (pos & 0x8000)) pos = -(pos & 0x7FFF);
            pos = pos % 4096;
            if (pos < 0) pos += 4096;
            return pos;
        }
    }
    return -1;
}

/* Base(9시) 이동 후: Present 암 위치가 목표(goal_arm)에서 많이 벗어나면 쳐박힘으로 보고 토크 해제.
 * goal은 mrwatchmaker_coords 의 9시 암 값과 같아야 함(고정 1079 가정 시 coords만 바꿔도 오탐). */
int motor_check_arm_stuck_after_9h(int goal_arm) {
    if (hSerial == INVALID_HANDLE_VALUE) return 0;
    if (goal_arm < 0) goal_arm = 0;
    if (goal_arm > 4095) goal_arm = 4095;
    Sleep(3200);
    int pos = motor_read_present_position(2);
    const int threshold = 200;
    if (pos >= 0 && abs(pos - goal_arm) > threshold) {
        motor_write_byte(2, 0x28, 0); /* Torque Enable = 0 (암 풀기) */
        return 1;
    }
    return 0;
}

/* 프로그램 종료/정지 시 토크를 확실히 꺼서 버징(달달) 지속을 방지 */
void motor_disable_torque_all(void) {
    if (hSerial == INVALID_HANDLE_VALUE) return;
    motor_write_byte(1, 0x28, 0);
    motor_write_byte(2, 0x28, 0);
}

void motor_close() {
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
}

#else
/* Linux: 스텁 구현 (시리얼 모터 미지원. 타임그래퍼/오디오만 사용 가능) */
static char port_buf[64] = "/dev/ttyUSB0";

const char *motor_get_port(void) {
	FILE *f = fopen("port.conf", "r");
	if (f) {
		char line[64];
		if (fgets(line, sizeof(line), f)) {
			int n = 0;
			if (sscanf(line, "%d", &n) == 1 && n >= 1 && n <= 99) {
				snprintf(port_buf, sizeof(port_buf), "/dev/ttyUSB%d", n - 1);
				fclose(f);
				return port_buf;
			}
			/* /dev/ttyUSB0 형태로 적혀 있으면 그대로 사용 */
			char *p = line;
			while (*p == ' ' || *p == '\t') p++;
			if (p[0] == '/') {
				size_t len = strlen(p);
				if (len > 0 && p[len-1] == '\n') p[--len] = '\0';
				if (len < sizeof(port_buf)) {
					strncpy(port_buf, p, sizeof(port_buf)-1);
					port_buf[sizeof(port_buf)-1] = '\0';
					fclose(f);
					return port_buf;
				}
			}
		}
		fclose(f);
	}
	return port_buf;
}

int motor_init(const char *port_name) {
	(void)port_name;
	/* Linux 시리얼 구현 없음: no-op */
	return 0;
}

void motor_move(uint8_t servo_id, int position, int time_ms, int speed) {
	(void)servo_id;
	(void)position;
	(void)time_ms;
	(void)speed;
}

void motor_write_byte(uint8_t servo_id, uint8_t addr, uint8_t value) {
	(void)servo_id;
	(void)addr;
	(void)value;
}

int motor_read_present_position(uint8_t servo_id) {
	(void)servo_id;
	return -1;
}

int motor_check_arm_stuck_after_9h(int goal_arm) {
	(void)goal_arm;
	return 0;
}

void motor_disable_torque_all(void) {
    /* no-op */
}

void motor_close() {
}
#endif
