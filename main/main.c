#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "switch.h"


void app_main(void){

	switch_init();

	while(1){
		if(switch_is_closed()) {
			printf("button is closed\n");
		} else {
			printf("switch is open\n");
		}
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}
