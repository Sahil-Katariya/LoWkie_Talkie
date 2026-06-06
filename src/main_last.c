#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/atomic.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "llcc68_driver.h"

LOG_MODULE_REGISTER(lora_audio_rx, LOG_LEVEL_INF);

static const struct gpio_dt_spec reset_pin =
    GPIO_DT_SPEC_GET(DT_ALIAS(lora0), reset_gpios);

static const struct device *i2s_dev =
    DEVICE_DT_GET(DT_NODELABEL(i2s20));

//  * AUDIO CONFIG

#define AUDIO_SAMPLE_RATE 8000
#define AUDIO_PAYLOAD_SIZE 200
#define AUDIO_RING_BUFFER_SIZE 32768

#define I2S_BLOCK_SIZE 1024
#define I2S_NUM_BLOCKS 8

#define PACKET_SYNC 0xA55A

#define RF_FREQUENCY 865000000

/*
 * SF7 + BW500 gives much better throughput
 * for audio streaming.
 */
#define LORA_SF 9
#define LORA_BW 250000
#define LORA_CR 1

K_MEM_SLAB_DEFINE(
    tx_mem_slab,
    I2S_BLOCK_SIZE,
    I2S_NUM_BLOCKS,
    4);

RING_BUF_DECLARE(audio_ringbuf, AUDIO_RING_BUFFER_SIZE);

#define AUDIO_THREAD_STACK_SIZE 4096
#define AUDIO_THREAD_PRIORITY 4

K_THREAD_STACK_DEFINE(audio_stack,
                      AUDIO_THREAD_STACK_SIZE);

static struct k_thread audio_thread_data;

// PACKET FORMAT

typedef struct __packed
{
    uint16_t sync;

    uint16_t packet_id;

    uint16_t payload_size;

    uint8_t payload[AUDIO_PAYLOAD_SIZE];

} audio_packet_t;

static uint16_t expected_packet = 0;

static atomic_t audio_started = ATOMIC_INIT(0);

static int i2s_init(void)
{
    if (!device_is_ready(i2s_dev))
    {
        LOG_ERR("I2S device not ready");

        return -ENODEV;
    }

    struct i2s_config config =
        {
            .word_size = 16,

            /*
             * Stereo output.
             * Many DACs/MAX98357 prefer stereo frames.
             */
            .channels = 2,
            .format = I2S_FMT_DATA_FORMAT_I2S,
            .options =
                I2S_OPT_BIT_CLK_MASTER |
                I2S_OPT_FRAME_CLK_MASTER,
            .frame_clk_freq = AUDIO_SAMPLE_RATE,
            .mem_slab = &tx_mem_slab,
            .block_size = I2S_BLOCK_SIZE,
            .timeout = 1000,
        };

    int ret = i2s_configure(
        i2s_dev,
        I2S_DIR_TX,
        &config);

    if (ret)
    {
        LOG_ERR("I2S configure failed: %d", ret);

        return ret;
    }

    LOG_INF("I2S initialized");

    return 0;
}

static int lora_init(void)
{
    peripherals_ready();

    LOG_INF("Initializing SX1262 RX");

    llcc68_reset(&reset_pin);

    llcc68_setStandby(LLCC68_STDBY_RC);

    if (llcc68_getMode() != LLCC68_STATUS_MODE_STDBY_RC)
    {
        LOG_ERR("Standby failed");

        return -EIO;
    }

    llcc68_setPacketType(LLCC68_PACKETTYPE_LORA);

    llcc68_fixResistanceAntenna();

    llcc68_setDio2AsRfSwitchCtrl(true);

    llcc68_setXtalCap(0x12, 0x12);

    llcc68_setFrequency(RF_FREQUENCY);

    llcc68_setTxPower(22,
                      SX126X_TX_POWER_SX1262);

    llcc68_LoraModulation(
        LORA_SF,
        LORA_BW,
        LORA_CR,
        false);

    llcc68_setLoraPacket(
        LLCC68_HEADER_EXPLICIT,
        8, // 12
        sizeof(audio_packet_t),
        true,
        false);

    llcc68_setSyncWord(0x3444);

    LOG_INF("SX1262 RX Ready");

    return 0;
}

//  * INSERT SILENCE FOR LOST PACKETS

static void insert_silence(uint32_t bytes)
{
    static uint8_t silence[AUDIO_PAYLOAD_SIZE];

    memset(silence, 128, sizeof(silence));

    while (bytes)
    {
        uint32_t chunk =
            MIN(bytes, sizeof(silence));

        uint32_t written = ring_buf_put(
            &audio_ringbuf,
            silence,
            chunk);
        if (written != chunk)
        {
            LOG_WRN("Ring buffer partial write chunk");
        }

        bytes -= chunk;
    }
}

//  * PROCESS RECEIVED PACKET

static void process_packet(uint8_t *data,
                           uint8_t len)
{
    if (len < 6)
    {
        return;
    }

    audio_packet_t *pkt =
        (audio_packet_t *)data;

    /* ---------------- SYNC ---------------- */

    if (pkt->sync != PACKET_SYNC)
    {
        return;
    }

    /* ---------------- SIZE ---------------- */

    if (pkt->payload_size >
        AUDIO_PAYLOAD_SIZE)
    {
        return;
    }

    /* ---------------- LOST PACKETS ---------------- */

    if (pkt->packet_id != expected_packet)
    {
        uint16_t lost =
            pkt->packet_id - expected_packet;

        LOG_WRN("Lost packets: %u", lost);

        insert_silence(
            lost * AUDIO_PAYLOAD_SIZE);

        expected_packet =
            pkt->packet_id;
    }

    /* ---------------- PUSH AUDIO ---------------- */

    uint32_t free_space =
        ring_buf_space_get(&audio_ringbuf);

    if (free_space <
        pkt->payload_size)
    {
        LOG_WRN("Ring buffer overflow");

        return;
    }

    uint32_t written = ring_buf_put(
        &audio_ringbuf,
        pkt->payload,
        pkt->payload_size);

    if (written != pkt->payload_size)
    {
        LOG_WRN("Ring buffer partial write");
    }

    expected_packet++;
}

//  * AUDIO THREAD

static void audio_thread(void *a,
                         void *b,
                         void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    bool i2s_started = false;

    while (1)
    {
        uint32_t available =
            ring_buf_size_get(
                &audio_ringbuf);

        /*
         * Prevent underrun.
         * Wait for enough audio.
         */
        if (available < 1024)
        {
            k_sleep(K_MSEC(5));

            continue;
        }

        void *block;

        int ret = k_mem_slab_alloc(
            &tx_mem_slab,
            &block,
            K_FOREVER);

        if (ret)
        {
            continue;
        }

        int16_t *samples =
            (int16_t *)block;

        uint32_t stereo_samples =
            I2S_BLOCK_SIZE /
            sizeof(int16_t);

        uint32_t mono_samples =
            stereo_samples / 2;

        uint8_t temp[mono_samples];

        uint32_t read =
            ring_buf_get(
                &audio_ringbuf,
                temp,
                mono_samples);

        /*
         * Convert:
         * unsigned 8-bit PCM
         * ->
         * signed 16-bit stereo PCM
         */
        for (uint32_t i = 0; i < read; i++)
        {
            int16_t s =
                ((int16_t)temp[i] - 128)
                << 8;

            /*
             * Duplicate mono to stereo
             */
            samples[i * 2] = s;
            samples[i * 2 + 1] = s;
        }

        /*
         * Pad remaining
         */
        for (uint32_t i = read;
             i < mono_samples;
             i++)
        {
            samples[i * 2] = 0;
            samples[i * 2 + 1] = 0;
        }

        ret = i2s_write(
            i2s_dev,
            block,
            I2S_BLOCK_SIZE);

        if (ret)
        {
            LOG_ERR("I2S write failed: %d",
                    ret);

            k_mem_slab_free(
                &tx_mem_slab,
                block);

            continue;
        }

        /*
         * Start once after first buffers queued
         */
        if (!i2s_started)
        {
            ret = i2s_trigger(
                i2s_dev,
                I2S_DIR_TX,
                I2S_TRIGGER_START);

            if (ret)
            {
                LOG_ERR("I2S start failed");

                continue;
            }

            i2s_started = true;

            LOG_INF("Audio playback started");
        }
    }
}

//  * MAIN

int main(void)
{
    int ret;

    LOG_INF("========== LoRa Audio RX ==========");

    ret = i2s_init();

    if (ret)
    {
        LOG_ERR("I2S init failed");

        return ret;
    }

    ret = lora_init();

    if (ret)
    {
        LOG_ERR("LoRa init failed");

        return ret;
    }

    /*
     * Dedicated audio thread
     */
    k_thread_create(
        &audio_thread_data,
        audio_stack,
        K_THREAD_STACK_SIZEOF(audio_stack),
        audio_thread,
        NULL,
        NULL,
        NULL,
        AUDIO_THREAD_PRIORITY,
        0,
        K_NO_WAIT);

    /*
     * MAIN RX LOOP
     */
    while (1)
    {
        llcc68_request(0U);

        llcc68_wait(0U);

        uint8_t len =
            llcc68_available();

        if (len == 0)
        {
            continue;
        }

        uint8_t rx_buffer[256];

        memset(rx_buffer,
               0,
               sizeof(rx_buffer));

        llcc68_read_data(
            rx_buffer,
            len);

        process_packet(
            rx_buffer,
            len);
    }

    return 0;
}