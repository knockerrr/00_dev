#include "deep_sleep_manager.h"
#include "switch.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DEEP_SLEEP_MGR";

#define TIMER_WAKEUP_TIME_US (24ULL * 60 * 60 * 1000000)

#define WAKEUP_GPIO_PIN GPIO_NUM_25

esp_err_t deep_sleep_manager_init(void)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    
    esp_err_t ret = switch_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Switch-Initialisierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Deep Sleep Manager erfolgreich initialisiert");
    return ESP_OK;
}

void handle_wakeup(void (*switch_func)(void), void (*timer_func)(void), void (*boot_rst_func)(void))
{
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wakeup Reason: %d", reason);
    
    // GPIO von RTC zu normal konvertieren wenn nötig
    if (reason == ESP_SLEEP_WAKEUP_EXT0) {
        rtc_gpio_deinit(WAKEUP_GPIO_PIN);
        ESP_LOGI(TAG, "GPIO%d von RTC zu normal konvertiert", WAKEUP_GPIO_PIN);
    }
    
    switch (reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(TAG, "=== SWITCH WAKEUP ===");
            if (switch_func != NULL) {
                switch_func();
            }
            break;
            
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "=== TIMER WAKEUP (24h) ===");
            if (timer_func != NULL) {
                timer_func();
            }
            break;
            
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            ESP_LOGI(TAG, "=== SYSTEM BOOT/RESET ===");
            if (boot_rst_func != NULL) {
                boot_rst_func();
            }
			break;
            
        default:
            ESP_LOGI(TAG, "=== UNKNOWN WAKEUP: %d ===", reason);
            break;
    }
}

void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Prepare Deep Sleep...");
    
    esp_err_t ret = esp_sleep_enable_timer_wakeup(TIMER_WAKEUP_TIME_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Timer Wakeup Configuration failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Timer Wakeup configured: 24 Stunden");
    }
    
    ESP_LOGI(TAG, "Convert GPIO%d to RTC GPIO for Deep Sleep", WAKEUP_GPIO_PIN);
    gpio_reset_pin(WAKEUP_GPIO_PIN);
    
    ret = rtc_gpio_init(WAKEUP_GPIO_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RTC GPIO Init failed: %s", esp_err_to_name(ret));
    }
    
    rtc_gpio_set_direction(WAKEUP_GPIO_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(WAKEUP_GPIO_PIN);
    rtc_gpio_pulldown_dis(WAKEUP_GPIO_PIN);
    ESP_LOGI(TAG, "RTC GPIO Pullup enabled for Pin %d", WAKEUP_GPIO_PIN);
    
    ret = esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EXT0 Wakeup configuration failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "EXT0 Wakeup configured: Pin %d, Level LOW", WAKEUP_GPIO_PIN);
    }
    
    ESP_LOGI(TAG, "Enter Deep Sleep...");
    ESP_LOGI(TAG, "Wakeup Sources: GPIO%d (LOW) or Timer (24h)", WAKEUP_GPIO_PIN);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    esp_deep_sleep_start();
}