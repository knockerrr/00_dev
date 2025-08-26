#ifndef PASSWORD_GENERATOR_H
#define PASSWORD_GENERATOR_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETUP_PASSWORD_LEN 8

/**
 * @brief Generate secure setup password based on hardware MAC address
 * @param password Buffer to store generated password (min 9 bytes for null terminator)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t generate_setup_password(char* password);

#ifdef __cplusplus
}
#endif

#endif // PASSWORD_GENERATOR_H