#ifndef SERIAL_MOTOR_H
#define SERIAL_MOTOR_H

#include <stdint.h>

const char *motor_get_port(void);  /* port.conf에서 읽거나 기본 COM5 */
int motor_init(const char *port_name);
void motor_move(uint8_t servo_id, int position, int time_ms, int speed);
void motor_write_byte(uint8_t servo_id, uint8_t addr, uint8_t value);  /* 토크 등 1바이트 쓰기 */
void motor_write_word(uint8_t servo_id, uint8_t addr, int value);     /* 2바이트 레지스터 (토크 리밋 등) */
void motor_set_visual_goal_deltas(int delta_face, int delta_arm); /* Android setVisualGoalDeltas: motor_move 시 목표에 더함 */
void motor_get_visual_goal_deltas(int *delta_face, int *delta_arm);
int  motor_read_present_position(uint8_t servo_id);  /* Present Position 읽기, 실패 시 -1 */
int  motor_check_arm_stuck_after_9h(int goal_arm);  /* 목표 암 틱과 Present 비교, 1=토크 해제함 */
void motor_disable_torque_all(void); /* ID1/ID2 토크 OFF */
void motor_release_motors_for_jam(void); /* Face 토크 먼저 끈 뒤 암 완전 해제 (Android 암 풀기와 동일) */
void motor_close();

#endif
