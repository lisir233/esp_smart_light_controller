#include "pwm_servo.h"
#include "driver/ledc.h"
#include "esp_err.h"
/* This is the paramater of the servo*/
#define PWM_SERVO_LEDC_TIMER             LEDC_TIMER_0
#define PWM_SERVO_LEDC_MODE              LEDC_LOW_SPEED_MODE
#define PWM_SERVO_IO                     (4) // Define the output GPIO
#define PWM_SERVO_LEDC_CHANNEL           LEDC_CHANNEL_0
#define PWM_SERVO_LEDC_DUTY_RES          LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define PWM_SERVO_LEDC_FREQUENCY         (100) // Frequency in Hertz. Set frequency at 100 Hz
#define PWM_SERVO_LEDC_DUTY_MIN          (410)  //0.5ms
#define PWM_SERVO_LEDC_DUTY_MAX          (2047) //2.5ms
void pwm_servo_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = PWM_SERVO_LEDC_MODE,
        .timer_num        = PWM_SERVO_LEDC_TIMER,
        .duty_resolution  = PWM_SERVO_LEDC_DUTY_RES,
        .freq_hz          = PWM_SERVO_LEDC_FREQUENCY,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_USE_RTC8M_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = PWM_SERVO_LEDC_MODE,
        .channel        = PWM_SERVO_LEDC_CHANNEL,
        .timer_sel      = PWM_SERVO_LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PWM_SERVO_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

}
void pwm_servo_set(int value)
{
    ESP_ERROR_CHECK(ledc_set_duty(PWM_SERVO_LEDC_MODE, PWM_SERVO_LEDC_CHANNEL, PWM_SERVO_LEDC_DUTY_MIN + (PWM_SERVO_LEDC_DUTY_MAX - PWM_SERVO_LEDC_DUTY_MIN) * value /100));
    // printf("pwm_set_duty %d\n",PWM_SERVO_LEDC_DUTY_MIN + (PWM_SERVO_LEDC_DUTY_MAX - PWM_SERVO_LEDC_DUTY_MIN) * value /100);
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(PWM_SERVO_LEDC_MODE, PWM_SERVO_LEDC_CHANNEL));
    
}