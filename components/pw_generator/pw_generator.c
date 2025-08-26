#include "password_generator.h"
#include "esp_system.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PWD_GEN";

esp_err_t generate_setup_password(char* password)
{
    if (!password) return ESP_ERR_INVALID_ARG;
    
    uint8_t mac[6];
    esp_err_t ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Generate 8-character password from MAC address
    // Use last 4 bytes of MAC for uniqueness
    snprintf(password, SETUP_PASSWORD_LEN + 1, "%02X%02X%02X%02X", 
             mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Setup password generated: %s", password);
    return ESP_OK;
}