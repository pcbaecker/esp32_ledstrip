/**
 * Based on https://github.com/CalinRadoni/esp32_digitalLEDs
 **/


#ifndef LEDSTRIP_H
#define LEDSTRIP_H

#include <cstdlib>
#include <cstdint>

#include "soc/rmt_struct.h"
#include "driver/rmt.h"
#include "driver/gpio.h"

/**
 * @brief The type of digital LEDs.
 *
 */
typedef enum {
        DLED_NULL, /*!< This is used as NOT set value */
        DLED_WS2812,
        DLED_WS2812B,
        DLED_WS2812D,
        DLED_WS2813,
        DLED_WS2815,
        DLED_WS281x /*!< This value should work for all WS281* and clones */
} dstrip_type_t;

/**
 * @brief Structure to be used as a pixel.
 *
 */
typedef struct {
        uint8_t r; /*!< Red color component */
        uint8_t g; /*!< Green color component */
        uint8_t b; /*!< Blue color component */
} pixel_t;

class LedStrip {
public:
/**
 *  @brief Constructor.
 **/
LedStrip(
        dstrip_type_t type,
        uint16_t numberOfLeds,
        gpio_num_t gpio,
        rmt_channel_t rmtChannel);

/**
 *  @brief Initializes the led strip.
 **/
bool init();

/**
 * @brief Sets a single pixel
 **/
void setPixel(uint16_t id, uint8_t r, uint8_t g, uint8_t b);

void update();

private:
/**
 * @brief Sets the timings for the specific led type.
 **/
void setTimings();

void byte_to_rmtitem(uint8_t data, uint16_t idx_in);

private:
/**
 *  @brief type of digital LEDs.
 **/
dstrip_type_t mType;

/**
 *  @brief The number of LEDs in the strip
 **/
uint16_t mNumberOfLeds = 0;

/**
 *  @brief These are the pixels, one for each LED.
 **/
pixel_t* pPixels = NULL;

/**
 * @brief buffer to hold data to be sent to LEDs.
 **/
uint8_t* pBuffer = NULL;

/**
 * @brief length, in bytes, of buffer.
 **/
uint16_t mBufferLength;

uint8_t max_cc_val; /*!< maximum value allowed for a color component */

/**
 *  @brief number of bytes per LED
 **/
uint8_t mBytesPerLed;
uint16_t T0H, T0L, T1H, T1L; /*!< timings of the communication protocol */
uint32_t TRS;            /*!< reset timing of the communication protocol */



/**
 * @brief The number of GPIO connected to the LED strip.
 **/
gpio_num_t mGpio;

/**
 * @brief The RMT channel to control the LED strip.
 **/
rmt_channel_t mRmtChannel;
rmt_item32_t rmtLO, rmtHI;  /*!< Values required to send 0 and 1 */
rmt_item32_t rmtLR, rmtHR;    /*!< Values required to send 0 and 1 including reset */

/**
 * @brief The buffer to be passed to the RMT driver for sending
 **/
rmt_item32_t* pRmtBuffer = NULL;

};

#endif
