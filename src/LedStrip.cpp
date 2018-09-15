#include "LedStrip.hpp"
#include "esp_log.h"

static const char *LOG_TAG  = "LedStrip";

/*
 * Standard APB clock (as needed by WiFi and BT to work) is 80 MHz.
 * This gives a 12.5 ns * rmt_config_t::clk_div for computing RMT durations, like this:
 *         duration = required_duration_in_ns / (12.5 * rmt_config_t::clk_div)
 */
const uint8_t rmt_clk_divider  = 4;
const uint16_t rmt_clk_duration = 50; // ns,  12.5 * 4(=config.clk_div in rmt_config_for_digital_led_strip)


LedStrip::LedStrip(dstrip_type_t type,
                   uint16_t numberOfLeds,
                   gpio_num_t gpio_number_in,
                   rmt_channel_t channel_in)
        :
        mType(type),
        mNumberOfLeds(numberOfLeds),
        mGpio(gpio_number_in),
        mRmtChannel(channel_in) {

}

bool LedStrip::init() {
        // Configure for the possible led types
        switch(this->mType) {
        case DLED_NULL:
                this->mBytesPerLed = 0;
                break;
        case DLED_WS2812:
        case DLED_WS2812B:
        case DLED_WS2812D:
        case DLED_WS2813:
        case DLED_WS2815:
        case DLED_WS281x:
                this->mBytesPerLed = 3;
                break;
        default:
                ESP_LOGE(LOG_TAG, "Unknown strip type");
                this->mType = DLED_NULL;
                return false;
        }

        // Create the pixel array
        this->pPixels = new pixel_t[this->mNumberOfLeds];
        if (this->pPixels == NULL) {
                ESP_LOGE(LOG_TAG, "Failed to allocate memory for pixels");
                return false;
        } else {
                ESP_LOGI(LOG_TAG, "Allocated %d bytes for pixels", sizeof(this->pPixels));
        }

        // Create the buffers
        this->mBufferLength = this->mNumberOfLeds * this->mBytesPerLed;
        this->pBuffer = new uint8_t[this->mBufferLength];
        if (this->pBuffer == NULL) {
                ESP_LOGE(LOG_TAG, "Failed to allocate memory for buffer");
                return false;
        } else {
                ESP_LOGI(LOG_TAG, "Allocated %d bytes for output buffer", this->mBufferLength);
        }

        // Set timings for the specific led type
        setTimings();

        // Set all pixels to off
        for (uint16_t i = 0; i < this->mNumberOfLeds; i++) {
                setPixel(i, 0, 0, 0);
        }

        // Initialize rmt buffer
        /* for every pixel are needed `8 * rps->strip->bytes_per_led` bits
         * for every bit is needed a `rmt_item32_t` */
        uint32_t req_length = this->mNumberOfLeds * 8 * this->mBytesPerLed * sizeof(rmt_item32_t);
        this->pRmtBuffer = (rmt_item32_t*)malloc(req_length);
        if (this->pRmtBuffer == NULL) {
                ESP_LOGE(LOG_TAG, "Failed to allocate memory for ugly buffer");
                return false;
        } else {
                ESP_LOGI(LOG_TAG, "Allocated %d bytes for ugly_buffer", req_length);
        }

// Set Rmt timings
        this->rmtLO.level0 = 1;
        this->rmtLO.level1 = 0;
        this->rmtLO.duration0 = this->T0H / rmt_clk_duration;
        this->rmtLO.duration1 = this->T0L / rmt_clk_duration;

        this->rmtHI.level0 = 1;
        this->rmtHI.level1 = 0;
        this->rmtHI.duration0 = this->T1H / rmt_clk_duration;
        this->rmtHI.duration1 = this->T1L / rmt_clk_duration;

        this->rmtLR.level0 = 1;
        this->rmtLR.level1 = 0;
        this->rmtLR.duration0 = this->T0H / rmt_clk_duration;
        this->rmtLR.duration1 = this->TRS / rmt_clk_duration;

        this->rmtHR.level0 = 1;
        this->rmtHR.level1 = 0;
        this->rmtHR.duration0 = this->T1H / rmt_clk_duration;
        this->rmtHR.duration1 = this->TRS / rmt_clk_duration;

        // Set gpio
        gpio_pad_select_gpio(this->mGpio);
        gpio_set_direction(this->mGpio, GPIO_MODE_OUTPUT);
        gpio_set_level(this->mGpio, 0);

        // Configure rmt
        rmt_config_t config;

        config.rmt_mode = rmt_mode_t::RMT_MODE_TX;
        config.channel  = this->mRmtChannel;
        config.clk_div  = rmt_clk_divider;
        config.gpio_num = this->mGpio;

        /* One memory block is 64 words * 32 bits each; the type is rmt_item32_t (defined in rmt_struct.h).
           A channel can use more memory blocks by taking from the next channels, so channel 0 can have 8
           memory blocks and channel 7 just one. */
        config.mem_block_num = 1;

        config.tx_config.loop_en              = false;
        config.tx_config.carrier_en           = false;
        config.tx_config.carrier_freq_hz      = 0;
        config.tx_config.carrier_duty_percent = 0;
        config.tx_config.carrier_level        = rmt_carrier_level_t::RMT_CARRIER_LEVEL_LOW;
        config.tx_config.idle_level           = rmt_idle_level_t::RMT_IDLE_LEVEL_LOW;
        config.tx_config.idle_output_en       = true;

        // stop this rmt channel
        esp_err_t ret_val;

        ret_val = rmt_rx_stop(this->mRmtChannel);
        if(ret_val != ESP_OK) {
                ESP_LOGE(LOG_TAG, "[0x%x] rmt_rx_stop failed", ret_val);
                return false;
        }
        ret_val = rmt_tx_stop(this->mRmtChannel);
        if(ret_val != ESP_OK) {
                ESP_LOGE(LOG_TAG, "[0x%x] rmt_tx_stop failed", ret_val);
                return false;
        }

        // disable rmt interrupts for this channel
        rmt_set_rx_intr_en(this->mRmtChannel, 0);
        rmt_set_err_intr_en(this->mRmtChannel, 0);
        rmt_set_tx_intr_en(this->mRmtChannel, 0);
        rmt_set_tx_thr_intr_en(this->mRmtChannel, 0, 0xffff);
        // set rmt memory to normal (not power-down) mode
        ret_val = rmt_set_mem_pd(this->mRmtChannel, false);
        if(ret_val != ESP_OK) {
                ESP_LOGE(LOG_TAG, "[0x%x] rmt_set_mem_pd failed", ret_val);
                return false;
        }

        /* The rmt_config function internally:
         * - enables the RMT module by calling periph_module_enable(PERIPH_RMT_MODULE);
         * - sets data mode with rmt_set_data_mode(RMT_DATA_MODE_MEM);
         * - associates the gpio pin with the rmt channel using rmt_set_pin(channel, mode, gpio_num);
         */
        ret_val = rmt_config(&config);
        if(ret_val != ESP_OK) {
                ESP_LOGE(LOG_TAG, "[0x%x] rmt_config failed", ret_val);
                return false;
        }

        ret_val = rmt_driver_install(this->mRmtChannel, 0, 0);
        if(ret_val != ESP_OK) {
                ESP_LOGE(LOG_TAG, "[0x%x] rmt_driver_install failed", ret_val);
                return false;
        }

        // Update all LEDs so that at the beginning they are off
        update();

        return true;
}

void LedStrip::setTimings() {
        /* Timings are from datasheets. DLED_WS281x timings should be good.
         * See https://cpldcpu.wordpress.com for interesting investigations about timings. */

        switch (this->mType) {
        case DLED_NULL:
                this->T0H = 0; this->T0L = 0; this->T1H = 0; this->T1L = 0; this->TRS = 0;
                break;
        case DLED_WS2812:
                this->T0H = 350; this->T0L = 800; this->T1H = 700; this->T1L = 600; this->TRS = 50000;
                break;
        case DLED_WS2812B:
        case DLED_WS2813:
        case DLED_WS2815:
                this->T0H = 300; this->T0L = 1090; this->T1H = 1090; this->T1L = 320; this->TRS = 280000;
                break;
        case DLED_WS2812D:
                this->T0H = 400; this->T0L = 850; this->T1H = 800; this->T1L = 450; this->TRS = 50000;
                break;
        case DLED_WS281x:
                this->T0H = 400; this->T0L = 850; this->T1H = 850; this->T1L = 400; this->TRS = 50000;
                break;
        }
}

void LedStrip::setPixel(uint16_t id, uint8_t r, uint8_t g, uint8_t b) {
        pixel_t& pixel = this->pPixels[id];
        pixel.r = r;
        pixel.g = g;
        pixel.b = b;
}

void LedStrip::byte_to_rmtitem(uint8_t data, uint16_t idx_in) {
        uint8_t mask = 0x80;
        uint16_t idx = idx_in;

        while (mask != 0) {
                this->pRmtBuffer[idx++] =
                        ((data & mask) != 0) ? this->rmtHI : this->rmtLO;
                mask = mask >> 1;
        }
}

void LedStrip::update() {
        uint16_t didx = 0;
        for(uint16_t i = 0; i < this->mNumberOfLeds; i++) {
                this->pBuffer[didx++] = this->pPixels[i].g;
                this->pBuffer[didx++] = this->pPixels[i].r;
                this->pBuffer[didx++] = this->pPixels[i].b;
        }


        didx = 0;
        for (uint16_t i = 0; i < this->mBufferLength; i++) {
                byte_to_rmtitem(this->pBuffer[i], didx);
                didx += 8;
        }

        // change last bit to include reset time
        didx--;
        if (this->pRmtBuffer[didx].val == this->rmtHI.val) {
                this->pRmtBuffer[didx] = this->rmtHR;
        } else {
                this->pRmtBuffer[didx] = this->rmtLR;
        }

        esp_err_t ret_val = rmt_write_items(this->mRmtChannel, this->pRmtBuffer, this->mBufferLength * 8, true);
        if(ret_val != ESP_OK) {
                ESP_LOGE(LOG_TAG, "[0x%x] rmt_write_items failed", ret_val);
        }
}
