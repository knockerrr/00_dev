#include "switch.h"
#include "driver/gpio.h"
#include "esp_log.h"
#define SWITCH_PIN GPIO_NUM_25


esp_err_t switch_init(void){
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << (int)SWITCH_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    esp_err_t ret = gpio_config(&io_config);
    if(ret != ESP_OK) {
        ESP_LOGE("SWITCH", "GPIO configuration failed with error: %d", ret);
        return ret;
    }
    ESP_LOGI("SWITCH", "GPIO configuration successful, SWITCH_PIN is configured as input with pull-up");
    return ESP_OK;
}

bool switch_is_closed(void) {
	return (gpio_get_level(SWITCH_PIN) == 0);

}
