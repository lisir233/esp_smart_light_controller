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
#include <freertos/queue.h>

#include "pwm_servo.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <esp_rmaker_utils.h>
#include "nvs_flash.h"
#include "nvs.h"
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
#define SERVO_EXECUTION_TIME 110    //ms
static bool g_pwm_servo_state = DEFAULT_PWM_SERVO_STATE;
int g_pwm_servo_up_level = DEFAULT_LIMIT_UP;
int g_pwm_servo_down_level = DEFAULT_LIMIT_DOWN;

static TaskHandle_t  servo_task_handle = NULL;

#define BATTERY_ADC_CHANNEL         ADC_CHANNEL_3
#define BATTERY_ADC_ATTEN           ADC_ATTEN_DB_11
#define BATTERY_ATTEN 0.5f   //V_bat = V_io / BATTERY_ATTEN

#define BATTERY_WARNING_VLOT 3.6f   //V_bat = V_io / BATTERY_ATTEN
#define BATTERY_SHUTDOWN_VLOT 3.4f   //V_bat = V_io / BATTERY_ATTEN

static esp_timer_handle_t button1_timer = NULL;
static gpio_int_type_t button1_intr_type = GPIO_INTR_HIGH_LEVEL;
#define BUTTON_DELAY_MIN_CONFIG  100000
#define BUTTON_DELAY_MAX_CONFIG  3000000
#define QCLOUD_NETWORK_CONFIG  5000000

#define REBOOT_DELAY        2
#define RESET_DELAY         2


/* These values correspoind to H,S,V = 120,100,10 */
#define DEFAULT_RED     0
#define DEFAULT_GREEN   25
#define DEFAULT_BLUE    0

#define WIFI_RESET_BUTTON_TIMEOUT       3
#define FACTORY_RESET_BUTTON_TIMEOUT    10

#define APP_DRIVER_QUEUE_SIZE 10
QueueHandle_t app_driver_evt_queue;

static void app_nvs_get_limit_value(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_driver", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        printf("Error opening NVS handle! %s\n",esp_err_to_name(err));
    } else {
        err = nvs_get_i32(nvs_handle, "up", &g_pwm_servo_up_level);
        if (err != ESP_OK) {
            printf("no value\n");

        } else {
            printf("read up value %d\n",g_pwm_servo_up_level);
        }
        err = nvs_get_i32(nvs_handle, "down", &g_pwm_servo_down_level);
        if (err != ESP_OK) {
            printf("no value\n");

        } else {
            printf("read down value %d\n",g_pwm_servo_down_level);
        }

        // 关闭NVS
        nvs_close(nvs_handle);
    }
}
void app_nvs_set_limit_value(int up,int down)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_driver", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        printf("Error opening NVS handle! %s\n",esp_err_to_name(err));
    } else {
        err = nvs_set_i32(nvs_handle, "up", up);
        if (err != ESP_OK) {
            printf("Error setting value!\n");
        } 
        err = nvs_set_i32(nvs_handle, "down", down);
        if (err != ESP_OK) {
            printf("Error setting value!\n");
        }

        // 提交更改
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            printf("Error committing data!\n");
        }
        // 关闭NVS
        nvs_close(nvs_handle);
    }
}
void app_indicator_set(bool state)
{
    if (state) {
        ws2812_led_set_rgb(DEFAULT_RED, DEFAULT_GREEN, DEFAULT_BLUE);
    } else {
        ws2812_led_clear();
    }
}
void app_servo_set_state(bool state){
   app_driver_evt_t app_driver_evt;
   app_driver_evt.event_type = APP_DRIVER_STATE_CHANGE;
   app_driver_evt.event_value = state;
   xQueueSend(app_driver_evt_queue, &app_driver_evt, 0);
}

static void servo_task(void* pvParameters)
{
   esp_pm_lock_handle_t servo_task_pm_lock;
   esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "servo", &servo_task_pm_lock);
   app_driver_evt_t app_driver_evt;
    while (1) {
        if (xQueueReceive(app_driver_evt_queue, &app_driver_evt, portMAX_DELAY)) {
            if(app_driver_evt.event_type == APP_DRIVER_STATE_CHANGE){
                esp_pm_lock_acquire(servo_task_pm_lock);
                gpio_set_level(SERVO_POWER_GPIO, 1);
                gpio_hold_en(SERVO_POWER_GPIO);
                g_pwm_servo_state = app_driver_evt.event_value;
                pwm_servo_set(g_pwm_servo_state == true? g_pwm_servo_up_level:g_pwm_servo_down_level);
                vTaskDelay(SERVO_EXECUTION_TIME/portTICK_PERIOD_MS);
                gpio_hold_dis(SERVO_POWER_GPIO);
                gpio_set_level(SERVO_POWER_GPIO, 0);
                esp_pm_lock_release(servo_task_pm_lock);
            }
            else if(app_driver_evt.event_type == APP_DRIVER_SET_SERVO_ON_LEVEL){
                esp_pm_lock_acquire(servo_task_pm_lock);
                gpio_set_level(SERVO_POWER_GPIO, 1);
                gpio_hold_en(SERVO_POWER_GPIO);
                g_pwm_servo_up_level = app_driver_evt.event_value;
                app_nvs_set_limit_value(g_pwm_servo_up_level,g_pwm_servo_down_level);
                pwm_servo_set(g_pwm_servo_up_level);
                vTaskDelay(SERVO_EXECUTION_TIME/portTICK_PERIOD_MS);
                gpio_hold_dis(SERVO_POWER_GPIO);
                gpio_set_level(SERVO_POWER_GPIO, 0);
                esp_pm_lock_release(servo_task_pm_lock);

            }else if(app_driver_evt.event_type == APP_DRIVER_SET_SERVO_OFF_LEVEL){
                esp_pm_lock_acquire(servo_task_pm_lock);
                gpio_set_level(SERVO_POWER_GPIO, 1);
                gpio_hold_en(SERVO_POWER_GPIO);
                g_pwm_servo_down_level = app_driver_evt.event_value;
                app_nvs_set_limit_value(g_pwm_servo_up_level,g_pwm_servo_down_level);
                pwm_servo_set(g_pwm_servo_down_level);
                vTaskDelay(SERVO_EXECUTION_TIME/portTICK_PERIOD_MS);
                gpio_hold_dis(SERVO_POWER_GPIO);
                gpio_set_level(SERVO_POWER_GPIO, 0);
                esp_pm_lock_release(servo_task_pm_lock);
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
    
}

static void set_power_state(bool target)
{
    gpio_set_level(OUTPUT_GPIO, target);
    app_indicator_set(target);
}
static void battery_monitor_task(void* pvParameters)
{

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = BATTERY_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BATTERY_ADC_CHANNEL, &config));
    int adc_raw;
    float battery_voltage;
    while(1){
        vTaskDelay(10*60*1000/portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BATTERY_ADC_CHANNEL, &adc_raw));
        battery_voltage = adc_raw * 3.0f / 4095 / BATTERY_ATTEN;
        //printf("Battery Voltage %f\r\n",battery_voltage);
        if(battery_voltage < BATTERY_SHUTDOWN_VLOT ){
            gpio_hold_dis(BOARD_POWER_IO);
            gpio_set_level(BOARD_POWER_IO, 0);
        }
        else if(battery_voltage < BATTERY_WARNING_VLOT){
            gpio_set_level(BOARD_LED_IO, 1);
            gpio_hold_en(BOARD_LED_IO);
            vTaskDelay(3000/portTICK_PERIOD_MS);
            gpio_set_level(BOARD_LED_IO, 1);
            gpio_hold_dis(BOARD_LED_IO);
        }
        else{
            gpio_set_level(BOARD_POWER_IO, 1);
            gpio_hold_en(BOARD_POWER_IO);
        }
    }
}
static void app_driver_button_long_press_callback(void* arg)
{
    esp_rmaker_factory_reset(RESET_DELAY, REBOOT_DELAY);
}
static void app_driver_button_click_callback(void* arg)
{
    push_btn_cb(NULL);
}
void IRAM_ATTR app_driver_button_isr(void *arg)
{
    static int64_t button_delay_time = -BUTTON_DELAY_MAX_CONFIG;
    if(button1_intr_type == GPIO_INTR_LOW_LEVEL){
        button1_intr_type = GPIO_INTR_HIGH_LEVEL;
        gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_HIGH_LEVEL);
        esp_timer_start_once(button1_timer, QCLOUD_NETWORK_CONFIG);
        button_delay_time = esp_timer_get_time();
    }else if (button1_intr_type == GPIO_INTR_HIGH_LEVEL) {
        esp_timer_stop(button1_timer);
        if(esp_timer_get_time() > button_delay_time + BUTTON_DELAY_MIN_CONFIG && 
           esp_timer_get_time() < button_delay_time + BUTTON_DELAY_MAX_CONFIG ){
            app_driver_button_click_callback(NULL);
            button_delay_time = esp_timer_get_time();
        }else{
            button_delay_time = esp_timer_get_time();
        }
        button1_intr_type = GPIO_INTR_LOW_LEVEL;
        gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
    }else {
        button1_intr_type = GPIO_INTR_HIGH_LEVEL;
        gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_HIGH_LEVEL);
    }
}

esp_err_t app_driver_button_config(gpio_num_t gpio_num)
{
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << gpio_num);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = true;
    return gpio_config(&io_conf);
}


esp_err_t app_driver_button_init(void)
{
    esp_err_t ret = ESP_FAIL;

    const esp_timer_create_args_t button_long_timer_args = {
            .callback = &app_driver_button_long_press_callback,
            .name = "button_long"
    };
    esp_timer_create(&button_long_timer_args, &button1_timer);

    ret = app_driver_button_config(BUTTON_GPIO);

    if(gpio_get_level(BUTTON_GPIO) == 0){
        button1_intr_type = GPIO_INTR_NEGEDGE;
    }

    ret = gpio_hold_en(BUTTON_GPIO);

    ret = gpio_install_isr_service(0);

    ret = gpio_isr_handler_add(BUTTON_GPIO, app_driver_button_isr, (void *)BUTTON_GPIO);
    ret = gpio_wakeup_enable(BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
    ret = esp_sleep_enable_gpio_wakeup();
    return ret;
}

void app_driver_init()
{
    app_nvs_get_limit_value();
    app_driver_evt_queue = xQueueCreate(APP_DRIVER_QUEUE_SIZE, sizeof(app_driver_evt_t));
    app_driver_button_init();

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
    xTaskCreate(battery_monitor_task, "battery_monitor_task", 2048, NULL, 12, NULL);

}

int IRAM_ATTR app_driver_set_state(bool state)
{
    if(g_power_state != state) {
        g_power_state = state;
        set_power_state(g_power_state);
    }
    app_servo_set_state(state);
    return ESP_OK;
}

bool app_driver_get_state(void)
{
    return g_power_state;
}

