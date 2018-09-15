#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "LedStrip.hpp"

extern "C" void app_main(void){
        const uint16_t numLeds = 300;
        nvs_flash_init();

        // Initialize the strip
        LedStrip strip(DLED_WS281x, numLeds, (gpio_num_t)GPIO_NUM_32, (rmt_channel_t)0);
        strip.init();

        // Wait for some time (maybe not neccessary?)
        vTaskDelay(250 / portTICK_PERIOD_MS);

        // Start sequence
        for (int j = 0; j < 8; j++) {
                for (int i = 0; i < numLeds; i++) {
                        uint l = i + j;
                        strip.setPixel(i, l % 2 == 0 ? 64 : 0, 0, l % 2 == 0 ? 0 : 64);
                }
                strip.update();
                vTaskDelay(250 / portTICK_PERIOD_MS);
        }

        // Clear all leds
        for (int i = 0; i < numLeds; i++) {
                strip.setPixel(i, 0, 0, 0);
        }
        strip.update();
        vTaskDelay(250 / portTICK_PERIOD_MS);

        // Show blue moving effect
        const int brightness = 128;
        int pos = 0;
        while (true) {
                // Go right
                while (pos < numLeds) {
                        for (int i = 0; i < numLeds; i++) {
                                int d = abs(pos - i) * (brightness / 16);
                                if (d > brightness) {
                                        d = brightness;
                                }
                                uint8_t localBrightness = brightness - d;
                                strip.setPixel(i, 0, 0, localBrightness);
                        }
                        strip.update();
                        pos++;
                        vTaskDelay(30 / portTICK_PERIOD_MS);
                }

                // Go left
                while (pos > 0) {
                        for (int i = 0; i < numLeds; i++) {
                                int d = abs(pos - i) * (brightness / 16);
                                if (d > brightness) {
                                        d = brightness;
                                }
                                uint8_t localBrightness = brightness - d;
                                strip.setPixel(i, 0, 0, localBrightness);
                        }
                        strip.update();
                        pos--;
                        vTaskDelay(30 / portTICK_PERIOD_MS);
                }
        }

        vTaskDelete(NULL);
}
