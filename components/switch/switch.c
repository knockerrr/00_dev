#include "switch.h"
#include "driver/gpio.h"

#define SWITCH_PIN GPIO_NUM_18


void switch_init(void){
	gpio_config_t io_config = {
		.pin_bit_mask = (1ULL << SWITCH_PIN),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE
	};
	gpio_config(&io_config);

}

bool switch_is_closed(void) {
	return gpio_get_level(SWITCH_PIN) == 0;
}
