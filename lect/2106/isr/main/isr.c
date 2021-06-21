#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
 
#define GPIO_OUT_0 18
#define GPIO_OUT_1 19
#define GPIO_OUT_SEL ((1ull << GPIO_OUT_0) | (1ull << GPIO_OUT_1))
#define GPIO_IN_0 33
#define GPIO_IN_1 25
#define GPIO_IN_SEL ((1ull << GPIO_IN_0) | (1ull << GPIO_IN_1))
#define ESP_INTR_FLAG_DEFAULT 0
#define QUEUE_SIZE (16u)

static TaskHandle_t isr_task_hnd;
static StaticTask_t isr_task;
static StackType_t isr_task_stack[4u * configMINIMAL_STACK_SIZE];

static StaticQueue_t gpio_evt_queue;
static uint8_t gpio_evt_queue_buffer[QUEUE_SIZE * sizeof (uint32_t)];
static QueueHandle_t gpio_evt_queue_hnd;
 
static void IRAM_ATTR gpio_isr_handler(void *arg) {
	uint32_t gpio_num = (uint32_t)arg;
	
	xQueueSendFromISR(gpio_evt_queue_hnd, &gpio_num, NULL);
	
	return;
}

static void gpio_task_example(void *arg) {
	uint32_t io_num;
	
	(void)arg;
	
	while (1) {
		if (xQueueReceive(gpio_evt_queue_hnd, &io_num, portMAX_DELAY))
			printf("GPIO %d intr, val %d\n", io_num, gpio_get_level(io_num));
	}
	
	vTaskDelete(NULL);
}
 
void app_main(void) {
	gpio_config_t io_conf;
	unsigned int i;
	
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = GPIO_OUT_SEL;
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);
	
	io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;
	io_conf.pin_bit_mask = GPIO_IN_SEL;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pull_up_en = 1;
	gpio_config(&io_conf);
	
	gpio_set_intr_type(GPIO_IN_0, GPIO_INTR_ANYEDGE);
	
	gpio_evt_queue_hnd = xQueueCreateStatic(QUEUE_SIZE, sizeof (uint32_t), gpio_evt_queue_buffer, &gpio_evt_queue);
	
	isr_task_hnd = xTaskCreateStatic(
			gpio_task_example,
			"gpio_isr_task",
			4u * configMINIMAL_STACK_SIZE,
			NULL,
			tskIDLE_PRIORITY + 10u,
			isr_task_stack,
			&isr_task
	);
	(void)isr_task_hnd;
	
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(GPIO_IN_0, gpio_isr_handler, (void *)GPIO_IN_0);
	gpio_isr_handler_add(GPIO_IN_1, gpio_isr_handler, (void *)GPIO_IN_1);
#if 0
	gpio_isr_handler_remove(GPIO_IN_0);
	gpio_isr_handler_add(GPIO_IN_0, gpio_isr_handler, (void *)GPIO_IN_0);
#endif
	
	for (i = 0; 1; i++) {
		printf("i = %u\n", i);
		vTaskDelay(100u / portTICK_RATE_MS);
		gpio_set_level(GPIO_OUT_0, i & 1u);
		gpio_set_level(GPIO_OUT_1, i & 1u);
	}
	
	vTaskDelete(NULL);
}

