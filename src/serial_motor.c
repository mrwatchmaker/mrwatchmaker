#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>

#ifdef _WIN32
#include <windows.h>

void motor_release_motors_for_jam(void);

/* 여러 스레드가 동시에 motor_init/close/read 하면 CH34x 등에서 실패·오탐 다이얼로그가 난다. */
static GRecMutex g_motor_io;
static gsize g_motor_io_once = 0;

static void motor_io_lock(void)
{
	if (g_once_init_enter(&g_motor_io_once)) {
		g_rec_mutex_init(&g_motor_io);
		g_once_init_leave(&g_motor_io_once, 1);
	}
	g_rec_mutex_lock(&g_motor_io);
}

static void motor_io_unlock(void)
{
	g_rec_mutex_unlock(&g_motor_io);
}

/* Android VisualCalibrationStore / ServoController.setVisualGoalDeltas 와 동일 */
static int g_face_goal_delta_ticks = 0;
static int g_arm_goal_delta_ticks = 0;

static int norm_pos_4096(int p) {
	p %= 4096;
	if (p < 0) p += 4096;
	return p;
}

void motor_set_visual_goal_deltas(int delta_face, int delta_arm) {
	g_face_goal_delta_ticks = delta_face;
	g_arm_goal_delta_ticks = delta_arm;
}

void motor_get_visual_goal_deltas(int *delta_face, int *delta_arm) {
	if (delta_face) *delta_face = g_face_goal_delta_ticks;
	if (delta_arm) *delta_arm = g_arm_goal_delta_ticks;
}

static HANDLE hSerial = INVALID_HANDLE_VALUE;
static char port_buf[16] = "COM5";  /* 기본값 */
static COMMTIMEOUTS g_io_timeouts;
static int g_io_timeouts_valid = 0;

/* PurgeComm(RX) 대신 즉시 반환 Read로 버퍼 비우기 — 일부 CH34x에서 Purge 후 ID2 응답이 사라지는 경우 완화 */
static void motor_drain_rx_soft(void) {
	COMMTIMEOUTS tq;
	uint8_t scratch[128];
	DWORD n;
	int k;

	if (hSerial == INVALID_HANDLE_VALUE)
		return;
	if (!g_io_timeouts_valid) {
		PurgeComm(hSerial, PURGE_RXCLEAR);
		return;
	}
	memset(&tq, 0, sizeof(tq));
	tq.ReadIntervalTimeout = MAXDWORD;
	tq.ReadTotalTimeoutMultiplier = 0;
	tq.ReadTotalTimeoutConstant = 0;
	if (!SetCommTimeouts(hSerial, &tq))
		return;
	for (k = 0; k < 32; k++) {
		if (!ReadFile(hSerial, scratch, sizeof(scratch), &n, NULL) || n == 0)
			break;
	}
	SetCommTimeouts(hSerial, &g_io_timeouts);
}

/* port.conf 한 줄: "COM3", "com3", "3" 등 (Linux 배포 예제처럼 숫자만 가능) */
static int parse_port_conf_line(char *line, int *out_n) {
	unsigned char *u = (unsigned char *)line;
	if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
		memmove(line, line + 3, strlen(line + 3) + 1);
	char *s = line;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = '\0';
	if (!*s) return 0;
	int n = 0;
	if (sscanf(s, "COM%d", &n) == 1 || sscanf(s, "com%d", &n) == 1) {
		if (n >= 1 && n <= 99) { *out_n = n; return 1; }
	}
	if (sscanf(s, "%d", &n) == 1 && n >= 1 && n <= 99) {
		*out_n = n;
		return 1;
	}
	return 0;
}

/* CreateFile용: COM10 이상은 \\.\COMn 필수 */
static void com_port_to_createfile_path(const char *com_port, char *out, size_t outsz) {
	int n = 0;
	const char *p = com_port;
	while (*p == ' ' || *p == '\t') p++;
	if (strncmp(p, "\\\\.\\", 4) == 0) {
		strncpy(out, p, outsz - 1);
		out[outsz - 1] = '\0';
		return;
	}
	if (sscanf(p, "COM%d", &n) != 1 && sscanf(p, "com%d", &n) != 1) {
		strncpy(out, p, outsz - 1);
		out[outsz - 1] = '\0';
		return;
	}
	if (n >= 10)
		snprintf(out, outsz, "\\\\.\\COM%d", n);
	else
		snprintf(out, outsz, "COM%d", n);
}

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
					int n = 0;
					if (parse_port_conf_line(line, &n)) {
						fclose(f);
						snprintf(port_buf, sizeof(port_buf), "COM%d", n);
						return port_buf;
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
    char win_path[32];
    int ret = 0;
    motor_io_lock();
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
    }
    com_port_to_createfile_path(port_name, win_path, sizeof(win_path));

    hSerial = CreateFileA(win_path,
                          GENERIC_READ | GENERIC_WRITE,
                          0,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
                          
    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        printf("Error opening serial port %s (CreateFile path=%s, err=%lu)\n",
               port_name, win_path, (unsigned long)err);
        motor_io_unlock();
        return 0;
    }
    
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        printf("Error getting state\n");
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        motor_io_unlock();
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
        motor_io_unlock();
        return 0;
    }
    
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = 50;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    
    SetCommTimeouts(hSerial, &timeouts);
    if (GetCommTimeouts(hSerial, &g_io_timeouts))
	    g_io_timeouts_valid = 1;
    else
	    g_io_timeouts_valid = 0;
    printf("Successfully opened %s at 1000000 baud\n", win_path);
    ret = 1;
    motor_io_unlock();
    return ret;
}

static uint8_t checksum(uint8_t *data, int len) {
    int sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i];
    }
    return (~sum) & 0xFF;
}

/* Android ServoController: 패킷 간 Sleep + RX에 쌓인 상태 응답 비우기.
 * FlushFileBuffers 는 일부 USB-시리얼(CH34x)에서 불안정. PURGE_TX 는 전송 중 패킷을 잘라
 * "조금 움직이다 통신 실패"를 유발하므로 TX purge 는 쓰지 않는다. */
static void motor_after_tx_packet(void) {
    if (hSerial == INVALID_HANDLE_VALUE) return;
    Sleep(40);
    PurgeComm(hSerial, PURGE_RXCLEAR);
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
    motor_io_lock();
    if (hSerial == INVALID_HANDLE_VALUE) {
        motor_io_unlock();
        return;
    }

    if (servo_id == 1)
        position = norm_pos_4096(position + g_face_goal_delta_ticks);
    else if (servo_id == 2)
        position = norm_pos_4096(position + g_arm_goal_delta_ticks);

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
    motor_after_tx_packet();
    motor_io_unlock();
}

/* STS3215: 2바이트 쓰기 (addr 48 = Torque Limit 등) */
void motor_write_word(uint8_t servo_id, uint8_t addr, int value) {
    motor_io_lock();
    if (hSerial == INVALID_HANDLE_VALUE) {
        motor_io_unlock();
        return;
    }
    int v = value & 0xFFFF;
    uint8_t packet[6];
    packet[0] = servo_id;
    packet[1] = 5;
    packet[2] = 0x03;
    packet[3] = addr;
    packet[4] = v & 0xFF;
    packet[5] = (v >> 8) & 0xFF;
    uint8_t chk = checksum(packet, 6);
    uint8_t full[9];
    full[0] = 0xFF;
    full[1] = 0xFF;
    for (int i = 0; i < 6; i++) full[2 + i] = packet[i];
    full[8] = chk;
    DWORD written;
    WriteFile(hSerial, full, 9, &written, NULL);
    motor_after_tx_packet();
    motor_io_unlock();
}

/* STS3215: 1바이트 쓰기 (addr 0x28 = Torque Enable 등) */
void motor_write_byte(uint8_t servo_id, uint8_t addr, uint8_t value) {
    motor_io_lock();
    if (hSerial == INVALID_HANDLE_VALUE) {
        motor_io_unlock();
        return;
    }
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
    motor_after_tx_packet();
    motor_io_unlock();
}

/* READ 응답: LEN=2 는 WRITE ACK 로 스킵. ID2·일부 펌웨어는 LEN 4~7 로 오기도 함(위치는 i+5,6 고정). */
static int parse_present_in_buffer(const uint8_t *buf, DWORD total, uint8_t servo_id) {
	for (DWORD i = 0; i + 8 <= total; i++) {
		if (buf[i] != 0xFF || buf[i + 1] != 0xFF || buf[i + 2] != servo_id)
			continue;
		{
			uint8_t plen = buf[i + 3];
			if (plen == 2)
				continue;
			if (plen < 4 || plen > 8)
				continue;
		}
		int pos_l = buf[i + 5] & 0xFF;
		int pos_h = buf[i + 6] & 0xFF;
		int pos = (pos_h << 8) | pos_l;
		if (pos > 4095 && (pos & 0x8000)) pos = -(pos & 0x7FFF);
		pos = pos % 4096;
		if (pos < 0) pos += 4096;
		return pos;
	}
	return -1;
}

/* STS3215: Present Position 읽기 (addr 0x38, 2바이트). 실패 시 -1 */
int motor_read_present_position(uint8_t servo_id) {
	int out = -1;
	motor_io_lock();
	if (hSerial == INVALID_HANDLE_VALUE) {
		motor_io_unlock();
		return -1;
	}
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

	for (int attempt = 0, max_attempts = (servo_id == 2) ? 10 : 6;
	     attempt < max_attempts;
	     attempt++) {
		motor_drain_rx_soft();
		{
			DWORD written = 0;
			if (!WriteFile(hSerial, full, 8, &written, NULL) || written != 8)
				continue;
		}
		/* 버스 끝 암(ID2)은 응답이 더 늦는 경우가 많음 */
		Sleep(servo_id == 2 ? 130 : 40);

		uint8_t buf[128];
		DWORD total = 0;
		for (int round = 0; round < 40; round++) {
			DWORD chunk = 0;
			if (!ReadFile(hSerial, buf + total, sizeof(buf) - total, &chunk, NULL))
				break;
			if (chunk > 0) {
				total += chunk;
				if (total >= 8) {
					int pos = parse_present_in_buffer(buf, total, servo_id);
					if (pos >= 0) {
						out = pos;
						goto read_done;
					}
				}
			} else {
				Sleep(15);
			}
			if (total >= sizeof(buf) - 4)
				break;
		}
		if (total >= 8) {
			int pos = parse_present_in_buffer(buf, total, servo_id);
			if (pos >= 0) {
				out = pos;
				goto read_done;
			}
		}
		Sleep(45);
	}
read_done:
	motor_io_unlock();
	return out;
}

/* Base(9시) 이동 후: Present 암 위치가 목표(goal_arm)에서 많이 벗어나면 쳐박힘으로 보고 토크 해제.
 * goal은 mrwatchmaker_coords 의 9시 암 값과 같아야 함(고정 1079 가정 시 coords만 바꿔도 오탐). */
int motor_check_arm_stuck_after_9h(int goal_arm) {
    int rv = 0;
    motor_io_lock();
    if (hSerial == INVALID_HANDLE_VALUE) {
        motor_io_unlock();
        return 0;
    }
    if (goal_arm < 0) goal_arm = 0;
    if (goal_arm > 4095) goal_arm = 4095;
    int expected_raw = norm_pos_4096(goal_arm + g_arm_goal_delta_ticks);
    Sleep(3200);
    int pos = motor_read_present_position(2);
    const int threshold = 200;
    if (pos >= 0 && abs(pos - expected_raw) > threshold) {
        motor_release_motors_for_jam();
        rv = 1;
    }
    motor_io_unlock();
    return rv;
}

/* Android ServoController.releaseMotorsForJam: Face(ID1) 0x28=0 후 암(ID2) 완전 해제 */
void motor_release_motors_for_jam(void) {
    motor_io_lock();
    if (hSerial == INVALID_HANDLE_VALUE) {
        motor_io_unlock();
        return;
    }
    int k;
    motor_write_byte(1, 0x28, 0);
    Sleep(15);
    for (k = 0; k < 8; k++) {
        motor_write_byte(2, 0x28, 0);
        Sleep(10);
    }
    motor_write_word(2, 48, 0);
    Sleep(10);
    motor_write_byte(2, 0x28, 0);
    motor_io_unlock();
}

/* 프로그램 종료/정지 시 토크를 확실히 꺼서 버징(달달) 지속을 방지 */
void motor_disable_torque_all(void) {
    motor_io_lock();
    if (hSerial == INVALID_HANDLE_VALUE) {
        motor_io_unlock();
        return;
    }
    /* ID2(암)가 간헐적으로 한 번에 안 풀리는 경우가 있어 반복 OFF를 보낸다. */
    motor_write_byte(1, 0x28, 0);
    Sleep(10);
    for (int k = 0; k < 8; k++) {
        motor_write_byte(2, 0x28, 0);
        Sleep(8);
    }
    motor_write_word(2, 48, 0);
    Sleep(8);
    motor_write_byte(2, 0x28, 0);
    motor_io_unlock();
}

void motor_close() {
    motor_io_lock();
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
    g_io_timeouts_valid = 0;
    motor_io_unlock();
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

void motor_write_word(uint8_t servo_id, uint8_t addr, int value) {
	(void)servo_id;
	(void)addr;
	(void)value;
}

void motor_write_byte(uint8_t servo_id, uint8_t addr, uint8_t value) {
	(void)servo_id;
	(void)addr;
	(void)value;
}

void motor_set_visual_goal_deltas(int delta_face, int delta_arm) {
	(void)delta_face;
	(void)delta_arm;
}

void motor_get_visual_goal_deltas(int *delta_face, int *delta_arm) {
	if (delta_face) *delta_face = 0;
	if (delta_arm) *delta_arm = 0;
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

void motor_release_motors_for_jam(void) {
}

void motor_close() {
}
#endif
