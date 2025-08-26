#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "deep_sleep_manager.h"
#include "switch.h"
#include "wifi_setup.h"

static const char *TAG = "MAIN";

// WiFi callback function to handle connection results
static void wifi_callback(bool success, esp_netif_ip_info_t* ip_info)
{
    if (success && ip_info) {
        ESP_LOGI(TAG, "WiFi connected successfully!");
        ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&ip_info->ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info->gw));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info->netmask));
    } else {
        ESP_LOGW(TAG, "WiFi connection failed or timed out");
    }
}

void func_switch(void)
{
    ESP_LOGI(TAG, "### START SWITCH ROUTINE ###");
    
    //###TODO###
    
    while(switch_is_closed())
    {
        //do nothing
    }
    
    ESP_LOGI(TAG, "### END SWITCH ROUTINE ###");
}

void func_scheduled(void)
{
    ESP_LOGI(TAG, "### START SCHEDULED ROUTINE ###");
    

    //###TODO###
    
    
    ESP_LOGI(TAG, "### END SCHEDULED ROUTINE ###");
}

void func_boot_rst(void)
{
    esp_err_t ret = wifi_setup_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi setup initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Check if we already have credentials
    if (wifi_setup_has_credentials()) {
        wifi_credentials_t creds;
        if (wifi_setup_get_credentials(&creds) == ESP_OK) {
            ESP_LOGI(TAG, "Found saved WiFi credentials for SSID: %s", creds.ssid);
        }
		ESP_LOGI(TAG, "Starting portal anyway for testing...");
	} else {
		ESP_LOGI(TAG, "No WiFi credentials saved, starting setup portal...");
    }
    
    // Start the WiFi setup portal - it will auto-timeout after 5 minutes
	ret = wifi_setup_start_portal(wifi_callback);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to start WiFi setup portal: %s", esp_err_to_name(ret));
		return;
    }
    
    ESP_LOGI(TAG, "WiFi setup portal started successfully!");
    ESP_LOGI(TAG, "WiFi test instructions:");
    ESP_LOGI(TAG, "1. Connect to 'ESP32-WiFi-Setup' network (check logs for password)");
    ESP_LOGI(TAG, "2. Open browser to http://192.168.4.1");
    ESP_LOGI(TAG, "3. Enter setup password and your WiFi credentials");
    ESP_LOGI(TAG, "4. Portal will auto-timeout after 5 minutes if unused");
    
    // Wait for the portal to complete or timeout (5 minutes max)
    ESP_LOGI(TAG, "Waiting for WiFi setup to complete...");
    
    
    // Check final state
    wifi_setup_state_t final_state = wifi_setup_get_state();
    if (final_state == WIFI_SETUP_STATE_CONNECTED) {
        ESP_LOGI(TAG, "WiFi setup completed successfully!");
    } else {
        ESP_LOGI(TAG, "WiFi setup finished with state: %d", final_state);
    }
    
    // Optionally clear credentials for testing (uncomment to test fresh setup)
    // ESP_LOGI(TAG, "Clearing WiFi credentials for testing...");
    // wifi_setup_clear_credentials();
    
    ESP_LOGI(TAG, "### END BOOT/RESET ROUTINE ###");
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== dev_00 GESTARTET (WiFi Test Mode) ===");
    
    esp_err_t ret = deep_sleep_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Deep Sleep Manager Initialisierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return;
    }
    
    // Handle wakeup reasons and run appropriate functions
    handle_wakeup(func_switch, func_scheduled, func_boot_rst);
    
    ESP_LOGI(TAG, "System setup completed.");
    ESP_LOGI(TAG, "Going to Deep Sleep in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    enter_deep_sleep();
    
    ESP_LOGE(TAG, "ERR: ENTERING DEEP SLEEP FAILED");
}