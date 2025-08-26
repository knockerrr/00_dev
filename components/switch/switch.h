/*switch.h*/
#ifndef SWITCH_H
#define SWITCH_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t switch_init(void);
bool switch_is_closed(void);


#endif //SWITCH_H
