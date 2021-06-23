#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#define SIZE (1u)
#define BUF_SIZE (128u)

typedef struct our_semaphore {
	StaticSemaphore_t mutex;
	SemaphoreHandle_t mutex_hnd;
	unsigned int val, max;
} our_semaphore;

static void semaphore_constructor(our_semaphore *s, unsigned int initial, unsigned int max) {
	s->max = max;
	s->val = initial;
	if (s->val > s->max) s->val = s->max;
	s->mutex_hnd = xSemaphoreCreateMutexStatic(&s->mutex);
	
	return;	
}

static void semaphore_take(our_semaphore *s) {
lock:
	xSemaphoreTake(s->mutex_hnd, portMAX_DELAY);
	if (!s->val) {
		xSemaphoreGive(s->mutex_hnd);
		goto lock;
	}
	s->val--;
	xSemaphoreGive(s->mutex_hnd);
	
	return;
}

static void semaphore_give(our_semaphore *s) {
	xSemaphoreTake(s->mutex_hnd, portMAX_DELAY);
	s->val++;
	if (s->val > s->max) s->val = s->max;
	xSemaphoreGive(s->mutex_hnd);
	
	return;	
}

typedef struct our_queue {
	our_semaphore countsem;
	our_semaphore spacesem;
	our_semaphore lock;
	size_t size;
	size_t datum_size;
	uint8_t *data;
	size_t in, out;
} our_queue;

static void queue_constructor(our_queue *q, size_t size, size_t datum_size, uint8_t *data) {
	q->size = size;
	q->datum_size = datum_size;
	q->data = data;
	q->in = q->out = 0u;

	semaphore_constructor(&q->lock, 1u, 1u);
	semaphore_constructor(&q->countsem, 0u, size);
	semaphore_constructor(&q->spacesem, size, size);
	
	return;
}

static void enqueue(our_queue *q, void *value) {
	size_t index;
	
	semaphore_take(&q->spacesem);
	
	semaphore_take(&q->lock);
#if 0
	index = (((q->in)++) & (q->size - 1u)) * q->datum_size;
#else
	index = (((q->in)++) % q->size) * q->datum_size;
#endif
	(void)memcpy(q->data + index, value, q->datum_size);
	semaphore_give(&q->lock);
	
	semaphore_give(&q->countsem);
	
	return;
}

static void dequeue(our_queue *q, void *value) {
	size_t index;
	
	semaphore_take(&q->countsem);
	
	semaphore_take(&q->lock);
#if 0
	index = (((q->out)++) & (q->size - 1u)) * q->datum_size;
#else
	index = (((q->out)++) % q->size) * q->datum_size;
#endif
	(void)memcpy(value, q->data + index, q->datum_size);
	semaphore_give(&q->lock);
	
	semaphore_give(&q->spacesem);
	
	return;
}

static TaskHandle_t producer_hnd, consumer0_hnd;
static StaticTask_t producer, consumer0;
static StackType_t producer_stack[4u * configMINIMAL_STACK_SIZE];
static StackType_t consumer0_stack[4u * configMINIMAL_STACK_SIZE];
static uint8_t q_buf[SIZE * BUF_SIZE];
static our_queue q;

void prod(void *_arg) {
	unsigned int data = 0u;
	char buf[BUF_SIZE];
	
	(void)_arg;
	
	while (1) {
		(void)snprintf(buf, BUF_SIZE, "%u", data++);
		enqueue(&q, buf);
		vTaskDelay(1000u / portTICK_PERIOD_MS);
	}
	
	vTaskDelete(NULL);
}

void cons(void *_arg) {
	unsigned int arg = (unsigned int)_arg;
	char buf[BUF_SIZE];
	
	(void)_arg;
	
	while (1) {
		dequeue(&q, buf);
		printf("consumption of \"%s\"\n", buf);
		vTaskDelay(arg / portTICK_PERIOD_MS);
	}
	
	vTaskDelete(NULL);
}

void app_main(void) {
	unsigned int i;
	
	queue_constructor(&q, SIZE, BUF_SIZE, q_buf);
	
	producer_hnd = xTaskCreateStatic(
			prod,
			"producer_task",
			4u * configMINIMAL_STACK_SIZE,
			NULL,
			tskIDLE_PRIORITY + 500u,
			producer_stack,
			&producer
	);
	
	consumer0_hnd = xTaskCreateStatic(
			cons,
			"consumer0_task",
			4u * configMINIMAL_STACK_SIZE,
			(void *)100u,
			tskIDLE_PRIORITY + 100u,
			consumer0_stack,
			&consumer0
	);
		
	(void)producer_hnd;
	(void)consumer0_hnd;
	
	for (i = 0u; 1; i++) { /* house keeping */
		printf("housekeeping %u...\n", i);
		vTaskDelay(10000u / portTICK_PERIOD_MS);
	}
	
	vTaskDelete(NULL);
}

