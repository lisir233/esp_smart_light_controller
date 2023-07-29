#ifndef PWM_SERVO_H
#define PWM_SERVO_H
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Init pwm Servo motor using ledc
 * @note  
 */
void pwm_servo_init(void);
/**
 * @brief Set pwm servo status.
 * @note  
 */
void pwm_servo_set(bool onoff);

#ifdef __cplusplus
}
#endif
#endif