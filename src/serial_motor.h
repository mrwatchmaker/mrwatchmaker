#ifndef SERIAL_MOTOR_H
#define SERIAL_MOTOR_H

#include <stdint.h>

const char *motor_get_port(void);  /* port.conf에서 읽거나 기본 COM5 */
int motor_init(const char *port_name);
void motor_move(uint8_t servo_id, int position, int time_ms, int speed);
void motor_write_byte(uint8_t servo_id, uint8_t addr, uint8_t value);  /* 토크 등 1바이트 쓰기 */
int  motor_read_present_position(uint8_t servo_id);  /* Present Position 읽기, 실패 시 -1 */
int  motor_check_arm_stuck_after_9h(void);  /* 9시 이동 후 쳐박힘 감지 시 암 토크 해제, 1=해제함 */
void motor_disable_torque_all(void); /* ID1/ID2 토크 OFF */
void motor_close();

#endif
