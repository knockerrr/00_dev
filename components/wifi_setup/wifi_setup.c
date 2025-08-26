#include "wifi_setup.h"
#include "pw_generator.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

static const char *TAG = "WIFI_SETUP";
static const char *NVS_NAMESPACE = "wifi_setup";
static const char *NVS_SSID_KEY = "ssid";
static const char *NVS_PASSWORD_KEY = "password";

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static httpd_handle_t server = NULL;
static esp_netif_t* ap_netif = NULL;
static esp_netif_t* sta_netif = NULL;
static wifi_setup_callback_t setup_callback = NULL;
static wifi_setup_state_t current_state = WIFI_SETUP_STATE_IDLE;
static EventGroupHandle_t wifi_event_group;
static int wifi_retry_num = 0;
static char setup_password[SETUP_PASSWORD_LEN + 1];
static TaskHandle_t timeout_task_handle = NULL;
static bool stay_connected_flag = false;
static uint32_t current_csrf_token = 0;

static uint32_t generate_csrf_token(void);
static void cleanup_wifi_resources(void);

// Timeout settings
#define PORTAL_TIMEOUT_MS (5 * 60 * 1000)  // 5 minutes for portal
#define CONNECT_TIMEOUT_MS (30 * 1000)     // 30 seconds after credentials entered

// Security: Rate limiting
static uint32_t last_save_attempt = 0;
static int save_attempt_count = 0;
#define MAX_SAVE_ATTEMPTS 5
#define RATE_LIMIT_WINDOW_MS 60000

// Simplified HTML with CSRF protection
static const char* setup_html_template = 
"<!DOCTYPE html>"
"<html><head>"
"<title>ESP32 WiFi Setup</title>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"body{font-family:Arial;margin:40px;background:#f0f0f0}"
".container{max-width:400px;margin:0 auto;background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
"h1{color:#333;text-align:center;margin-bottom:30px}"
"input{width:100%%;padding:12px;margin:8px 0;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:16px}"
"button{width:100%%;padding:15px;background:#007bff;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;margin-top:10px}"
"button:hover{background:#0056b3}"
".info{background:#e7f3ff;padding:15px;border-radius:5px;margin-bottom:20px;color:#31708f;font-size:14px}"
".error{background:#f8d7da;padding:15px;border-radius:5px;margin-bottom:20px;color:#721c24;font-size:14px}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>📶 WiFi Setup</h1>"
"<div class='info'>Connect ESP32 to your WiFi network. Password required: <strong>%s</strong></div>"
"<form action='/save' method='post'>"
"<input type='password' name='setup_pwd' placeholder='Setup Password' required maxlength='8'>"
"<input type='text' name='ssid' placeholder='WiFi Network Name' required maxlength='31'>"
"<input type='password' name='password' placeholder='WiFi Password' required maxlength='63'>"
"<input type='hidden' name='csrf' value='%08x'>"
"<button type='submit'>Save & Connect</button>"
"</form>"
"</div></body></html>";

static const char* success_html = 
"<!DOCTYPE html><html><head><title>Success</title><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<meta http-equiv='refresh' content='3;url=/'><style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0}"
".success{background:#d4edda;padding:20px;border-radius:5px;color:#155724;max-width:400px;margin:0 auto}</style></head>"
"<body><div class='success'><h2>✅ Success!</h2>Connecting to WiFi...</div></body></html>";

// Timeout task for automatic WiFi disable
static void timeout_task(void* param) {
    uint32_t timeout_ms = (uint32_t)(uintptr_t)param;
    
    ESP_LOGI(TAG, "WiFi timeout set for %lu ms", timeout_ms);
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
    
    if (current_state == WIFI_SETUP_STATE_CONNECTED && !stay_connected_flag) {
        ESP_LOGI(TAG, "WiFi timeout - disconnecting");
        wifi_setup_disconnect();
        if (setup_callback) {
            setup_callback(false, NULL); // Notify timeout
        }
    } else if (current_state == WIFI_SETUP_STATE_PORTAL_RUNNING) {
        ESP_LOGI(TAG, "Portal timeout - stopping portal");
        wifi_setup_stop_portal();
        current_state = WIFI_SETUP_STATE_DISABLED;
        if (setup_callback) {
            setup_callback(false, NULL); // Notify timeout
        }
    }
    
    timeout_task_handle = NULL;
    vTaskDelete(NULL);
}

// Start timeout task
static void start_timeout_task(uint32_t timeout_ms) {
    if (timeout_task_handle) {
        vTaskDelete(timeout_task_handle);
        timeout_task_handle = NULL;
    }
    
    xTaskCreate(timeout_task, "wifi_timeout", 2048, 
                (void*)(uintptr_t)timeout_ms, 3, &timeout_task_handle);
}

// Stop timeout task
static void stop_timeout_task(void) {
    if (timeout_task_handle) {
        vTaskDelete(timeout_task_handle);
        timeout_task_handle = NULL;
        ESP_LOGI(TAG, "Timeout task stopped");
    }
}



// URL decode function
static void url_decode(char* str) {
    char* src = str;
    char* dst = str;
    
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
        } else if (*src == '%' && src[1] && src[2]) {
            int high = (src[1] >= '0' && src[1] <= '9') ? src[1] - '0' : 
                       (src[1] >= 'A' && src[1] <= 'F') ? src[1] - 'A' + 10 :
                       (src[1] >= 'a' && src[1] <= 'f') ? src[1] - 'a' + 10 : 0;
            int low = (src[2] >= '0' && src[2] <= '9') ? src[2] - '0' : 
                      (src[2] >= 'A' && src[2] <= 'F') ? src[2] - 'A' + 10 :
                      (src[2] >= 'a' && src[2] <= 'f') ? src[2] - 'a' + 10 : 0;
            *dst++ = (high << 4) | low;
            src += 3;
            continue;
        } else {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

static void cleanup_wifi_resources(void)
{
    stop_timeout_task();
    
    if (current_state != WIFI_SETUP_STATE_DISABLED) {
        esp_wifi_stop();
        esp_wifi_deinit();
        
        if (sta_netif) {
            esp_netif_destroy_default_wifi(sta_netif);
            sta_netif = NULL;
        }
        
        // Unregister event handlers
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
        
        current_state = WIFI_SETUP_STATE_DISABLED;
        ESP_LOGI(TAG, "WiFi resources cleaned up");
    }
}

static uint32_t generate_csrf_token(void)
{
    return esp_random();
	ESP_LOGE(TAG, "Failed to generate CSRF Token!");
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_num < 3 && current_state == WIFI_SETUP_STATE_CONNECTING) {
            esp_wifi_connect();
            wifi_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi... (%d/3)", wifi_retry_num);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to WiFi");
            current_state = WIFI_SETUP_STATE_FAILED;
            
            // Auto-disconnect after failure
            vTaskDelay(pdMS_TO_TICKS(1000));
            cleanup_wifi_resources();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retry_num = 0;
        current_state = WIFI_SETUP_STATE_CONNECTED;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        
        // Start timeout for auto-disconnect (unless staying connected)
        if (!stay_connected_flag) {
            start_timeout_task(CONNECT_TIMEOUT_MS);
        }
        
        if (setup_callback) {
            setup_callback(true, &event->ip_info);
        }
    }
}

// HTTP GET handler
static esp_err_t setup_get_handler(httpd_req_t *req)
{
    current_csrf_token = generate_csrf_token();
    
    char html_buffer[2048];
    snprintf(html_buffer, sizeof(html_buffer), setup_html_template, 
             setup_password, current_csrf_token);
    
    // Security headers
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "text/html");
    
    return httpd_resp_send(req, html_buffer, strlen(html_buffer));
}

static void wifi_connect_task(void* param)
{
    vTaskDelay(pdMS_TO_TICKS(1000)); // Let response send
    
    // Stop portal and connect to WiFi
    current_state = WIFI_SETUP_STATE_CONNECTING;
    wifi_setup_stop_portal();
    
    // Get credentials and connect
    wifi_credentials_t* creds = (wifi_credentials_t*)param;
    stay_connected_flag = false; // Default: auto-disconnect after timeout
    
    // Connect using the new function
    esp_err_t connect_err = wifi_setup_connect(setup_callback, false);
    if (connect_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connection");
        if (setup_callback) {
            setup_callback(false, NULL);
        }
    }
    
    free(param);
    vTaskDelete(NULL);
}

// HTTP POST handler with security checks
static esp_err_t save_post_handler(httpd_req_t *req)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Rate limiting
    if (now - last_save_attempt < RATE_LIMIT_WINDOW_MS) {
        save_attempt_count++;
        if (save_attempt_count > MAX_SAVE_ATTEMPTS) {
            ESP_LOGW(TAG, "Rate limit exceeded");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Too many attempts");
            return ESP_FAIL;
        }
    } else {
        save_attempt_count = 1;
    }
    last_save_attempt = now;
    
    char buf[512];
    int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse form data
    char setup_pwd[16] = {0};
    char csrf_str[16] = {0};
    wifi_credentials_t creds = {0};
    
    // Extract setup password
    char *setup_pwd_start = strstr(buf, "setup_pwd=");
    if (setup_pwd_start) {
        setup_pwd_start += 10;
        char *setup_pwd_end = strchr(setup_pwd_start, '&');
        int len = setup_pwd_end ? setup_pwd_end - setup_pwd_start : strlen(setup_pwd_start);
        len = len > 15 ? 15 : len;
        strncpy(setup_pwd, setup_pwd_start, len);
        url_decode(setup_pwd);
    }
    
    // Verify setup password
    if (strcmp(setup_pwd, setup_password) != 0) {
        ESP_LOGW(TAG, "Invalid setup password");
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid password");
        return ESP_FAIL;
    }
    
    // Extract CSRF token
    char *csrf_start = strstr(buf, "csrf=");
    if (csrf_start) {
        csrf_start += 5;
        char *csrf_end = strchr(csrf_start, '&');
        int len = csrf_end ? csrf_end - csrf_start : strlen(csrf_start);
        len = len > 15 ? 15 : len;
        strncpy(csrf_str, csrf_start, len);
    }
    
    uint32_t received_csrf = strtoul(csrf_str, NULL, 16);
    if (received_csrf != current_csrf_token) {
        ESP_LOGW(TAG, "CSRF token mismatch");
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid request");
        return ESP_FAIL;
    }
    
    // Extract WiFi credentials
    char *ssid_start = strstr(buf, "ssid=");
    char *password_start = strstr(buf, "password=");
    
    if (!ssid_start || !password_start) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing data");
        return ESP_FAIL;
    }
    
    // Extract SSID
    ssid_start += 5;
    char *ssid_end = strchr(ssid_start, '&');
    if (ssid_end) {
        int len = ssid_end - ssid_start;
        len = len > WIFI_SSID_MAX_LEN - 1 ? WIFI_SSID_MAX_LEN - 1 : len;
        strncpy(creds.ssid, ssid_start, len);
        url_decode(creds.ssid);
    }
    
    // Extract Password  
    password_start += 9;
    char *password_end = strchr(password_start, '&');
    int len = password_end ? password_end - password_start : strlen(password_start);
    len = len > WIFI_PASSWORD_MAX_LEN - 1 ? WIFI_PASSWORD_MAX_LEN - 1 : len;
    strncpy(creds.password, password_start, len);
    url_decode(creds.password);
    
    // Validate credentials
    if (strlen(creds.ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Received WiFi credentials: SSID='%s'", creds.ssid);
    
    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
        return ESP_FAIL;
    }
    
    nvs_set_str(nvs_handle, NVS_SSID_KEY, creds.ssid);
    nvs_set_str(nvs_handle, NVS_PASSWORD_KEY, creds.password);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi credentials saved");
    
    // Send success response
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, success_html, strlen(success_html));
    
    // Start WiFi connection in background task
    wifi_credentials_t* creds_copy = malloc(sizeof(wifi_credentials_t));
	if (creds_copy) {
		memcpy(creds_copy, &creds, sizeof(wifi_credentials_t));
		xTaskCreate(wifi_connect_task, "wifi_connect", 4096, creds_copy, 5, NULL);
	} else {
		ESP_LOGE(TAG, "Failed to allocate memory for credentials");
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
    return ESP_FAIL;
	}
    
    return ESP_OK;
}

esp_err_t wifi_setup_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    wifi_event_group = xEventGroupCreate();
    
    // Generate setup password from MAC
    ret = generate_setup_password(setup_password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate setup password");
        return ret;
    }
    
    ESP_LOGI(TAG, "WiFi Setup initialized. Setup password: %s", setup_password);
    return ESP_OK;
}

bool wifi_setup_has_credentials(void)
{
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) != ESP_OK) {
        return false;
    }
    
    size_t required_size;
    esp_err_t err = nvs_get_str(nvs_handle, NVS_SSID_KEY, NULL, &required_size);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK && required_size > 1);
}

esp_err_t wifi_setup_get_credentials(wifi_credentials_t* creds)
{
    if (!creds) return ESP_ERR_INVALID_ARG;
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;
    
    size_t ssid_len = sizeof(creds->ssid);
    size_t password_len = sizeof(creds->password);
    
    err = nvs_get_str(nvs_handle, NVS_SSID_KEY, creds->ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, NVS_PASSWORD_KEY, creds->password, &password_len);
    }
    
    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_setup_start_portal(wifi_setup_callback_t callback)
{
    setup_callback = callback;
    current_state = WIFI_SETUP_STATE_PORTAL_RUNNING;
    stay_connected_flag = false;
    
    ESP_LOGI(TAG, "Starting secure WiFi setup portal...");
    ESP_LOGI(TAG, "Setup password: %s", setup_password);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    ap_netif = esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    // Configure secure AP
    wifi_config_t wifi_config = {
		.ap = {
			.ssid = "ESP32-WiFi-Setup",
			.ssid_len = strlen("ESP32-WiFi-Setup"),
			.max_connection = 1,
			.authmode = WIFI_AUTH_WPA2_PSK,
			.beacon_interval = 100,
		},
	};
    
    // Set password
    strncpy((char*)wifi_config.ap.password, setup_password, sizeof(wifi_config.ap.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi AP started: %s (Password: %s)", wifi_config.ap.ssid, setup_password);
    
    // Start timeout for portal (5 minutes)
    start_timeout_task(PORTAL_TIMEOUT_MS);
    
    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.server_port = 80;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t setup_uri = {.uri = "/", .method = HTTP_GET, .handler = setup_get_handler};
        httpd_uri_t save_uri = {.uri = "/save", .method = HTTP_POST, .handler = save_post_handler};
        
        httpd_register_uri_handler(server, &setup_uri);
        httpd_register_uri_handler(server, &save_uri);
        
        ESP_LOGI(TAG, "Secure setup portal running at http://192.168.4.1");
        ESP_LOGI(TAG, "Portal will timeout in 5 minutes");
    } else {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

void wifi_setup_stop_portal(void)
{
    stop_timeout_task();
    
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    
    esp_wifi_stop();
    esp_wifi_deinit();
    
    if (ap_netif) {
        esp_netif_destroy_default_wifi(ap_netif);
        ap_netif = NULL;
    }
    
    current_state = WIFI_SETUP_STATE_IDLE;
    ESP_LOGI(TAG, "WiFi setup portal stopped");
}

esp_err_t wifi_setup_connect(wifi_setup_callback_t callback, bool stay_connected)
{
    if (current_state == WIFI_SETUP_STATE_CONNECTED) {
        ESP_LOGW(TAG, "WiFi already connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!wifi_setup_has_credentials()) {
        ESP_LOGE(TAG, "No WiFi credentials stored");
        return ESP_ERR_NOT_FOUND;
    }
    
    wifi_credentials_t creds;
    esp_err_t err = wifi_setup_get_credentials(&creds);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi credentials: %s", esp_err_to_name(err));
        return err;
    }
    
    setup_callback = callback;
    stay_connected_flag = stay_connected;
    current_state = WIFI_SETUP_STATE_CONNECTING;
    wifi_retry_num = 0;
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s (stay_connected: %s)", 
             creds.ssid, stay_connected ? "true" : "false");
    
    // Initialize networking if not already done
    if (!esp_netif_get_default_netif()) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    }
    
    // Create STA interface
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    }
    
    // Configure WiFi
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, creds.ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, creds.password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi connection attempt started");
    return ESP_OK;
}

void wifi_setup_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting WiFi");
    cleanup_wifi_resources();
    
    if (setup_callback) {
        setup_callback(false, NULL); // Notify disconnection
    }
}

esp_err_t wifi_setup_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;
    
    nvs_erase_key(nvs_handle, NVS_SSID_KEY);
    nvs_erase_key(nvs_handle, NVS_PASSWORD_KEY);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ESP_OK;
}

wifi_setup_state_t wifi_setup_get_state(void)
{
    return current_state;
}