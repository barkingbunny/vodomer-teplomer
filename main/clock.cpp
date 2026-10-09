#include "clock.h"

#include <sys/time.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint64_t clockNow() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return uint64_t(tv.tv_sec) * 1000000ULL + uint64_t(tv.tv_usec);
}

uint32_t uptimeMs() {
    return uint32_t(esp_timer_get_time() / 1000);
}

void delayMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}
