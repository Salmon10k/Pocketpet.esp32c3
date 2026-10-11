#pragma once
// Minimal task API mock. The host checker only compiles firmware; it does not run tasks.
#include <freertos/FreeRTOS.h>
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
static inline BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *created) {
  if (created) *created = (TaskHandle_t)1;
  return pdPASS;
}
static inline void vTaskDelay(TickType_t) {}
