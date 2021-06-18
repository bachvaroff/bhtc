#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#define SIZE (32u)

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
	StaticSemaphore_t countsem;
	SemaphoreHandle_t countsem_hnd;
	StaticSemaphore_t spacesem;
	SemaphoreHandle_t spacesem_hnd;
	StaticSemaphore_t lock;
	SemaphoreHandle_t lock_hnd;
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

#if 0
	q->lock_hnd = xSemaphoreCreateMutexStatic(&q->lock);
#else
	q->lock_hnd = xSemaphoreCreateCountingStatic(1u, 0u, &q->lock);
	xSemaphoreGive(q->lock_hnd);	
#endif
	q->countsem_hnd = xSemaphoreCreateCountingStatic(size, 0u, &q->countsem);
	q->spacesem_hnd = xSemaphoreCreateCountingStatic(size, size, &q->spacesem);
	
	return;
}

/*
	|int 0|int 1|…|int 15| (SIZE == 16)
	SIZE == 16u * sizeof (int)
	|b0|b1|b2|b3|←0
	            |b4|b5|b6|b7|←1
	                        |b8|b9|b10|b11|←2
	…
	                         …|b60|b61|b62|b63|←15
	in = 0;
	((in++) & (SIZE - 1u)) * sizeof (int) → 0
	((in++) & (SIZE - 1u)) * sizeof (int) → 4
	((in++) & (SIZE - 1u)) * sizeof (int) → 8
*/

static void enqueue(our_queue *q, void *value) {
	size_t index;
	
	xSemaphoreTake(q->spacesem_hnd, portMAX_DELAY);
	
	xSemaphoreTake(q->lock_hnd, portMAX_DELAY);
#if 0
	index = (((q->in)++) & (q->size - 1u)) * q->datum_size;
#else
	index = (((q->in)++) % q->size) * q->datum_size;
#endif
	(void)memcpy(q->data + index, value, q->datum_size);
	xSemaphoreGive(q->lock_hnd);
	
	xSemaphoreGive(q->countsem_hnd);
	
	return;
}

static void dequeue(our_queue *q, void *value) {
	size_t index;
	
	xSemaphoreTake(q->countsem_hnd, portMAX_DELAY);
	
	xSemaphoreTake(q->lock_hnd, portMAX_DELAY);
#if 0
	index = (((q->out)++) & (q->size - 1u)) * q->datum_size;
#else
	index = (((q->out)++) % q->size) * q->datum_size;
#endif
	(void)memcpy(value, q->data + index, q->datum_size);
	xSemaphoreGive(q->lock_hnd);
	
	xSemaphoreGive(q->spacesem_hnd);
	
	return;
}

typedef struct our_data {
	int a;
	short b;
} our_data;

static TaskHandle_t producer_hnd, consumer0_hnd, consumer1_hnd;
static StaticTask_t producer, consumer0, consumer1;
static StackType_t producer_stack[4u * configMINIMAL_STACK_SIZE];
static StackType_t consumer0_stack[4u * configMINIMAL_STACK_SIZE];
static StackType_t consumer1_stack[4u * configMINIMAL_STACK_SIZE];
static uint8_t q_buf[SIZE * sizeof (our_data)];
static our_queue q;

void prod(void *_arg) {
	our_data val;
	int j;
	
	(void)_arg;
	
	val.a = 0;
	val.b = 0;
	
	while (1) {
		for (j = 0; j < 16; j++) {
			printf("enqueue %d %hd\n", val.a, val.b);
			enqueue(&q, &val);
			val.a++;
			val.b++;
		}
		vTaskDelay(1000u / portTICK_PERIOD_MS);
	}
	
	vTaskDelete(NULL);
}

void cons(void *_arg) {
	our_data val;
	unsigned int arg = (unsigned int)_arg;
	
	while (1) {
		dequeue(&q, &val);
		printf("consumption %d of %d %hd\n", arg, val.a, val.b);
		vTaskDelay(arg / portTICK_PERIOD_MS);
	}
	
	vTaskDelete(NULL);
}

void app_main(void) {
	unsigned int i;
	
	queue_constructor(&q, SIZE, sizeof (our_data), q_buf);
	
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
	
	consumer1_hnd = xTaskCreateStatic(
			cons,
			"consumer1_task",
			4u * configMINIMAL_STACK_SIZE,
			(void *)10u,
			tskIDLE_PRIORITY + 10u,
			consumer1_stack,
			&consumer1
	);
	
	(void)producer_hnd;
	(void)consumer0_hnd;
	(void)consumer1_hnd;
	
	for (i = 0u; 1; i++) { /* house keeping */
		printf("housekeeping %u...\n", i);
		vTaskDelay(200u / portTICK_PERIOD_MS);
	}
	
	vTaskDelete(NULL);
}

