#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64

/**
 * @brief WiFi credentials structure for storing network information
 */
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];         ///< Network name (SSID)
    char password[WIFI_PASSWORD_MAX_LEN]; ///< Network password
} wifi_credentials_t;

/**
 * @brief WiFi setup component states
 */
typedef enum {
    WIFI_SETUP_STATE_IDLE,          ///< Component initialized but inactive
    WIFI_SETUP_STATE_PORTAL_RUNNING,///< Captive portal active for credential input
    WIFI_SETUP_STATE_CONNECTING,    ///< Attempting to connect to stored WiFi network
    WIFI_SETUP_STATE_CONNECTED,     ///< Successfully connected to WiFi network
    WIFI_SETUP_STATE_FAILED,        ///< Connection attempt failed
    WIFI_SETUP_STATE_DISABLED       ///< WiFi completely disabled to save power
} wifi_setup_state_t;

/**
 * @brief Callback function type for WiFi setup completion events
 * @param success true if WiFi connection successful, false on failure/timeout
 * @param ip_info IP address information when connected (NULL on failure)
 */
typedef void (*wifi_setup_callback_t)(bool success, esp_netif_ip_info_t* ip_info);

/**
 * @brief Initialize the WiFi setup component
 * 
 * Performs initial setup including:
 * - NVS (Non-Volatile Storage) initialization for credential storage
 * - Generates unique setup password based on device MAC address
 * - Creates necessary FreeRTOS event groups for WiFi state management
 * 
 * @return esp_err_t ESP_OK on successful initialization, error code otherwise
 * 
 * @note This function must be called before any other WiFi setup functions
 * @note The generated setup password will be logged for user reference
 */
esp_err_t wifi_setup_init(void);

/**
 * @brief Check if WiFi credentials are stored in non-volatile memory
 * 
 * Queries NVS storage to determine if valid WiFi credentials have been
 * previously saved during a setup session.
 * 
 * @return true if valid credentials exist in storage, false otherwise
 * 
 * @note Returns false if NVS cannot be accessed or credentials are empty
 * @note Use this to decide whether to start setup portal or connect directly
 */
bool wifi_setup_has_credentials(void);

/**
 * @brief Retrieve stored WiFi credentials from non-volatile memory
 * 
 * Reads previously saved WiFi network credentials from NVS storage
 * and populates the provided structure.
 * 
 * @param creds Pointer to wifi_credentials_t structure to fill with stored data
 * @return esp_err_t ESP_OK if credentials retrieved successfully
 *                   ESP_ERR_INVALID_ARG if creds pointer is NULL
 *                   ESP_ERR_NOT_FOUND if no credentials are stored
 *                   Other ESP error codes for NVS access failures
 * 
 * @note Call wifi_setup_has_credentials() first to verify credentials exist
 * @note Retrieved credentials are not validated - they may be for non-existent networks
 */
esp_err_t wifi_setup_get_credentials(wifi_credentials_t* creds);

/**
 * @brief Start the WiFi setup captive portal for credential input
 * 
 * Creates a secure WiFi access point and web server for users to input
 * their home network credentials via smartphone/computer browser.
 * 
 * Portal features:
 * - Creates "ESP32-WiFi-Setup" network with MAC-based password protection
 * - Serves responsive web interface at http://192.168.4.1
 * - Implements CSRF protection and rate limiting for security
 * - Automatically times out after 5 minutes if unused
 * - Transitions to STA mode and connects after credential submission
 * 
 * @param callback Function to call when setup completes or times out
 * @return esp_err_t ESP_OK if portal started successfully, error code otherwise
 * 
 * @note Portal automatically stops and connects to submitted network
 * @note Connection attempt times out after 30 seconds unless stay_connected=true
 * @note Only one client can connect to setup portal simultaneously
 */
esp_err_t wifi_setup_start_portal(wifi_setup_callback_t callback);

/**
 * @brief Manually stop the WiFi setup portal
 * 
 * Immediately shuts down the captive portal access point and web server,
 * cancels any running timeout timers, and cleans up resources.
 * 
 * @note Automatically called when credentials are submitted via portal
 * @note Safe to call even if portal is not running
 * @note Does not affect stored credentials - only stops the portal interface
 */
void wifi_setup_stop_portal(void);

/**
 * @brief Connect to WiFi network using stored credentials
 * 
 * Attempts connection to the WiFi network using previously stored credentials.
 * Connection behavior depends on stay_connected parameter:
 * 
 * stay_connected = false (default):
 * - Connects to network and retrieves IP address
 * - Automatically disconnects after 30 seconds to save power
 * - Ideal for quick data uploads or brief network operations
 * 
 * stay_connected = true:
 * - Connects and remains connected indefinitely
 * - Suitable for continuous network operations
 * - Higher power consumption due to active WiFi radio
 * 
 * @param callback Function to call with connection result and IP info
 * @param stay_connected If true, maintain connection; if false, auto-disconnect after 30s
 * @return esp_err_t ESP_OK if connection attempt started successfully
 *                   ESP_ERR_NOT_FOUND if no credentials are stored
 *                   ESP_ERR_INVALID_STATE if already connected
 *                   Other ESP error codes for initialization failures
 * 
 * @note Requires credentials to be stored via wifi_setup_start_portal() first
 * @note Connection attempts are retried up to 3 times before failing
 * @note Callback is invoked for both success and failure scenarios
 */
esp_err_t wifi_setup_connect(wifi_setup_callback_t callback, bool stay_connected);

/**
 * @brief Immediately disconnect from WiFi and disable radio
 * 
 * Forcibly disconnects from current WiFi network, stops all WiFi operations,
 * and disables the WiFi radio to minimize power consumption.
 * 
 * Actions performed:
 * - Stops any active connection attempts or established connections
 * - Cancels running timeout timers
 * - Deallocates WiFi driver resources and network interfaces
 * - Sets component state to WIFI_SETUP_STATE_DISABLED
 * - Triggers callback with success=false to notify disconnection
 * 
 * @note Safe to call regardless of current WiFi state
 * @note Does not delete stored credentials - use wifi_setup_clear_credentials()
 * @note WiFi can be re-enabled later via wifi_setup_connect()
 */
void wifi_setup_disconnect(void);

/**
 * @brief Delete stored WiFi credentials from non-volatile memory
 * 
 * Permanently removes saved network credentials from NVS storage.
 * Use this to reset the device to factory WiFi settings or when
 * switching to a different network.
 * 
 * @return esp_err_t ESP_OK if credentials cleared successfully
 *                   Error codes for NVS access failures
 * 
 * @note Does not affect current WiFi connection - call wifi_setup_disconnect() first
 * @note After clearing, wifi_setup_has_credentials() will return false
 * @note User will need to go through setup portal again for new credentials
 */
esp_err_t wifi_setup_clear_credentials(void);

/**
 * @brief Get current state of the WiFi setup component
 * 
 * Returns the current operational state to help coordinate application logic
 * with WiFi operations and power management decisions.
 * 
 * @return wifi_setup_state_t Current component state:
 *         - WIFI_SETUP_STATE_IDLE: Ready for operations
 *         - WIFI_SETUP_STATE_PORTAL_RUNNING: Setup portal active
 *         - WIFI_SETUP_STATE_CONNECTING: Attempting network connection  
 *         - WIFI_SETUP_STATE_CONNECTED: Successfully connected to network
 *         - WIFI_SETUP_STATE_FAILED: Last connection attempt failed
 *         - WIFI_SETUP_STATE_DISABLED: WiFi radio disabled for power saving
 * 
 * @note State changes are managed automatically by the component
 * @note Use for application logic decisions and user interface updates
 */
wifi_setup_state_t wifi_setup_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_SETUP_H