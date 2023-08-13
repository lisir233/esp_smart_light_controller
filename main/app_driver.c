/* Switch demo implementation using button and RGB LED
   
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sdkconfig.h>

#include <iot_button.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h> 

#include <app_reset.h>
#include <ws2812_led.h>
#include <esp_log.h>
#include "app_priv.h"
#include "pwm_servo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pwm_servo.h"
static const char *TAG = "app_driver";
/* This is the button that is used for toggling the power */
#define BUTTON_GPIO          CONFIG_EXAMPLE_BOARD_BUTTON_GPIO
#define BUTTON_ACTIVE_LEVEL  0

/* This is the GPIO on which the power will be set */
#define OUTPUT_GPIO    CONFIG_EXAMPLE_OUTPUT_GPIO
static bool g_power_state = DEFAULT_POWER;


#define BOARD_POWER_IO    5
#define BOARD_LED_IO    7
/* This is the GPIO on which the servo power will be set */
#define SERVO_POWER_GPIO    6
#define DEFAULT_PWM_SERVO_STATE false
#define DEFAULT_SERVO_POWER_STATE false
#define SERVO_EXECUTION_TIME 150    //ms
static bool g_pwm_servo_state = DEFAULT_PWM_SERVO_STATE;
static TaskHandle_t  servo_task_handle = NULL;

typedef enum
{
    SERVO_EXECUTION_OFF = 0,
    SERVO_EXECUTION_ON,
    SERVO_EXECUTION_TOGGLE,
    SERVO_EXECUTION_CLICK,
} servo_execution_t;


/* These values correspoind to H,S,V = 120,100,10 */
#define DEFAULT_RED     0
#define DEFAULT_GREEN   25
#define DEFAULT_BLUE    0

#define WIFI_RESET_BUTTON_TIMEOUT       3
#define FACTORY_RESET_BUTTON_TIMEOUT    10

void app_indicator_set(bool state)
{
    if (state) {
        ws2812_led_set_rgb(DEFAULT_RED, DEFAULT_GREEN, DEFAULT_BLUE);
    } else {
        ws2812_led_clear();
    }
}
void app_servo_set_state(bool state){
    xTaskNotify(servo_task_handle, (uint32_t)state,eSetValueWithoutOverwrite);
}
void app_servo_click(void)
{
    xTaskNotify(servo_task_handle, (uint32_t)SERVO_EXECUTION_CLICK,eSetValueWithoutOverwrite);
}
static void servo_task(void* pvParameters){
   static servo_execution_t servo_execution = SERVO_EXECUTION_OFF;
    while(1){
        if(xTaskNotifyWait(0,0,(uint32_t *)&servo_execution,portMAX_DELAY) == pdTRUE){
            if(servo_execution == SERVO_EXECUTION_OFF){
                ESP_LOGI(TAG, "SERVO_EXECUTION_OFF");
                gpio_set_level(SERVO_POWER_GPIO, 1);
                g_pwm_servo_state = false;
                pwm_servo_set(g_pwm_servo_state);
                vTaskDelay(SERVO_EXECUTION_TIME/portTICK_PERIOD_MS);
                gpio_set_level(SERVO_POWER_GPIO, 0);
            }else if(servo_execution == SERVO_EXECUTION_ON){
                ESP_LOGI(TAG, "SERVO_EXECUTION_ON");
                gpio_set_level(SERVO_POWER_GPIO, 1);
                g_pwm_servo_state = true;
                pwm_servo_set(g_pwm_servo_state);
                vTaskDelay(SERVO_EXECUTION_TIME/portTICK_PERIOD_MS);
                gpio_set_level(SERVO_POWER_GPIO, 0);

            }else if(servo_execution == SERVO_EXECUTION_TOGGLE){
                ESP_LOGI(TAG, "SERVO_EXECUTION_TOGGLE");
                gpio_set_level(SERVO_POWER_GPIO, 1);
                g_pwm_servo_state = !g_pwm_servo_state;
                pwm_servo_set(g_pwm_servo_state);
                vTaskDelay(SERVO_EXECUTION_TIME/portTICK_PERIOD_MS);
                gpio_set_level(SERVO_POWER_GPIO, 0);
            }else if(servo_execution == SERVO_EXECUTION_CLICK){
                ESP_LOGI(TAG, "SERVO_EXECUTION_CLICK");
                gpio_set_level(SERVO_POWER_GPIO, 1);
                g_pwm_servo_state = true;
                pwm_servo_set(g_pwm_servo_state);
                vTaskDelay(SERVO_EXECUTION_TIME/portTICK_PERIOD_MS);
                g_pwm_servo_state = false;
                pwm_servo_set(g_pwm_servo_state);
                vTaskDelay(SERVO_EXECUTION_TIME/portTICK_PERIOD_MS);
                gpio_set_level(SERVO_POWER_GPIO, 0);
            }
        }
    }

}
void app_servo_init(void)
{
    /* Init pwm servo*/
    pwm_servo_init();
    /* Init pwm power */
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
    };
    io_conf.pin_bit_mask = ((uint64_t)1 << SERVO_POWER_GPIO);
    /* Configure the GPIO */
    gpio_config(&io_conf);
    xTaskCreate(servo_task, "servo_task", 2048, NULL, 15, &servo_task_handle);
    
    app_servo_set_state(DEFAULT_PWM_SERVO_STATE);
}
static void app_indicator_init(void)
{
    ws2812_led_init();
    app_indicator_set(g_power_state);
}
static void push_btn_cb(void *arg)
{
    bool new_state = !g_power_state;
    app_driver_set_state(new_state);
    esp_rmaker_param_update_and_report(
    esp_rmaker_device_get_param_by_name(switch_device, ESP_RMAKER_DEF_POWER_NAME),
    esp_rmaker_bool(new_state));
    app_homekit_update_state(new_state);
    
}

static void set_power_state(bool target)
{
    gpio_set_level(OUTPUT_GPIO, target);
    app_indicator_set(target);
}

void app_driver_init()
{
    button_handle_t btn_handle = iot_button_create(BUTTON_GPIO, BUTTON_ACTIVE_LEVEL);
    if (btn_handle) {
        /* Register a callback for a button tap (short press) event */
        iot_button_set_evt_cb(btn_handle, BUTTON_CB_TAP, push_btn_cb, NULL);
        /* Register Wi-Fi reset and factory reset functionality on same button */
        app_reset_button_register(btn_handle, WIFI_RESET_BUTTON_TIMEOUT, FACTORY_RESET_BUTTON_TIMEOUT);
    }

    /* Configure power */
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
    };
    io_conf.pin_bit_mask = ((uint64_t)1 << OUTPUT_GPIO);
    /* Configure the GPIO */
    gpio_config(&io_conf);

    io_conf.pull_up_en = 0;
    io_conf.pin_bit_mask = ((uint64_t)1 << BOARD_POWER_IO);
    /* Configure the GPIO */
    gpio_config(&io_conf);
    
    io_conf.pull_up_en = 0;
    io_conf.pin_bit_mask = ((uint64_t)1 << BOARD_LED_IO);
    /* Configure the GPIO */
    gpio_config(&io_conf);
    gpio_set_level(BOARD_POWER_IO, 1);
    gpio_hold_en(BOARD_POWER_IO);
    gpio_set_level(BOARD_LED_IO, 1);
    app_indicator_init();
    app_servo_init();
}

int IRAM_ATTR app_driver_set_state(bool state)
{
    if(g_power_state != state) {
        g_power_state = state;
        set_power_state(g_power_state);
    }
    app_servo_click();
    return ESP_OK;
}

bool app_driver_get_state(void)
{
    return g_power_state;
}

