#pragma once
// Minimal recursive-mutex API mock for host-side syntax checks only.
#include <freertos/FreeRTOS.h>
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return (SemaphoreHandle_t)1; }
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) { return pdTRUE; }
