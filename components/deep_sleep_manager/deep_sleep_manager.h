#ifndef DEEP_SLEEP_MANAGER_H
#define DEEP_SLEEP_MANAGER_H

#include "esp_err.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wake-up reasons enumeration
 * Uses ESP-IDF's built-in esp_sleep_wakeup_cause_t
 */
typedef esp_sleep_wakeup_cause_t wakeup_reason_t;

/**
 * @brief Initialisiert das Deep Sleep Management System
 * 
 * @return esp_err_t ESP_OK bei Erfolg
 */
esp_err_t deep_sleep_manager_init(void);

/**
 * @brief Startet den Deep Sleep Modus
 * Konfiguriert beide Wakeup-Quellen (GPIO25 und 24h Timer)
 */
void enter_deep_sleep(void);

/**
 * @brief Verarbeitet das Aufwachen und führt entsprechende Funktion aus
 * Sollte als erstes in app_main() aufgerufen werden
 * 
 * @param switch_func Funktion die beim Schalter-Wakeup ausgeführt wird
 * @param timer_func Funktion die beim Timer-Wakeup (24h) ausgeführt wird
 */
void handle_wakeup(void (*switch_func)(void), void (*timer_func)(void));

#ifdef __cplusplus
}
#endif

#endif // DEEP_SLEEP_MANAGER_H