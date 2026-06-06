#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2s.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "llcc68_driver.h"

LOG_MODULE_REGISTER(lora_rx, LOG_LEVEL_INF);

static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_GET(DT_ALIAS(lora0), reset_gpios);

static const struct device *i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s0));

#define I2S_BLOCK_SIZE 512
#define I2S_NUM_BLOCKS 4

K_MEM_SLAB_DEFINE(
    tx_mem_slab,
    I2S_BLOCK_SIZE,
    I2S_NUM_BLOCKS,
    4);

/* ------------------------------------------------ */

#define AUDIO_PAYLOAD_SIZE 64

#define MAX_AUDIO_SIZE 16384

#define PACKET_SYNC 0xA55A

/* ------------------------------------------------ */

typedef struct __packed
{
    uint16_t sync;

    uint16_t packet_id;

    uint16_t payload_size;

    uint8_t payload[AUDIO_PAYLOAD_SIZE];

} audio_packet_t;

/* ------------------------------------------------ */

static audio_packet_t rx_packet;

static uint8_t audio_buffer[MAX_AUDIO_SIZE];

static uint32_t received_bytes = 0;

static uint16_t expected_packet = 0;

/* ------------------------------------------------ */

static int i2s_init(void)
{
    if (!device_is_ready(i2s_dev))
    {
        LOG_ERR("I2S device not ready");

        return -1;
    }

    struct i2s_config config = {
        .word_size = 16,
        .channels = 1,
        .format = I2S_FMT_DATA_FORMAT_I2S,
        .options =
            I2S_OPT_BIT_CLK_MASTER |
            I2S_OPT_FRAME_CLK_MASTER,
        .frame_clk_freq = 8000,
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
        LOG_ERR("I2S configure failed");

        return ret;
    }

    LOG_INF("I2S initialized");

    return 0;
}
/* ------------------------------------------------ */

static int lora_init(void)
{
    peripherals_ready();

    LOG_INF("Initializing SX1262 RX");

    llcc68_reset(&reset_pin);

    llcc68_setStandby(LLCC68_STDBY_RC);

    uint8_t mode = llcc68_getMode();

    if (mode != LLCC68_STATUS_MODE_STDBY_RC)
    {
        LOG_ERR("Standby failed");

        return -1;
    }

    llcc68_setPacketType(LLCC68_PACKETTYPE_LORA);

    llcc68_fixResistanceAntenna();

    llcc68_setDio2AsRfSwitchCtrl(true);

    llcc68_setXtalCap(0x12, 0x12);

    llcc68_setFrequency(865000000);

    llcc68_setTxPower(22, SX126X_TX_POWER_SX1262);

    llcc68_LoraModulation(9, 250000, 1, false);

    llcc68_setLoraPacket(LLCC68_HEADER_EXPLICIT, 8, sizeof(audio_packet_t), true, false);

    llcc68_setSyncWord(0x3444);

    LOG_INF("SX1262 RX Ready");

    return 0;
}

/* ------------------------------------------------ */

static void process_packet(uint8_t *data, uint8_t len)
{
    /*
     * Minimum packet size:
     * sync + packet_id + payload_size
     */
    if (len < 6)
    {
        LOG_WRN("Small packet");

        return;
    }

    memcpy(&rx_packet, data, len);

    /* ---------------- SYNC CHECK ---------------- */

    if (rx_packet.sync != PACKET_SYNC)
    {
        LOG_ERR(
            "Bad sync 0x%04X",
            rx_packet.sync);

        return;
    }

    /* ---------------- SEQUENCE CHECK ---------------- */

    if (rx_packet.packet_id != expected_packet)
    {
        LOG_ERR(
            "Packet mismatch expected=%u got=%u",
            expected_packet,
            rx_packet.packet_id);

        expected_packet =
            rx_packet.packet_id + 1;

        return;
    }

    /* ---------------- PAYLOAD VALIDATION ---------------- */

    if (rx_packet.payload_size >
        AUDIO_PAYLOAD_SIZE)
    {
        LOG_ERR("Invalid payload size");

        return;
    }

    /* ---------------- BUFFER OVERFLOW ---------------- */

    if ((received_bytes +
         rx_packet.payload_size) >
        MAX_AUDIO_SIZE)
    {
        LOG_ERR("Audio buffer overflow");

        return;
    }

    /* ---------------- COPY AUDIO ---------------- */

    memcpy(
        &audio_buffer[received_bytes],
        rx_packet.payload,
        rx_packet.payload_size);

    received_bytes +=
        rx_packet.payload_size;

    int16_t rssi =
        llcc68_packetRssi();

    int8_t snr =
        (int8_t)llcc68_snr();

    LOG_INF(
        "RX Packet=%u Size=%u Total=%u RSSI=%d SNR=%d",
        rx_packet.packet_id,
        rx_packet.payload_size,
        received_bytes,
        rssi,
        snr);

    expected_packet++;
}
/* ------------------------------------------------ */

static void play_audio(void)
{
    // LOG_INF("Starting playback");

    uint32_t offset = 0;

    while (offset < received_bytes)
    {
        void *block;

        int ret = k_mem_slab_alloc(
            &tx_mem_slab,
            &block,
            K_FOREVER);

        if (ret)
        {
            LOG_ERR("Slab alloc failed");

            return;
        }

        int16_t *samples =
            (int16_t *)block;

        uint32_t samples_per_block =
            I2S_BLOCK_SIZE / sizeof(int16_t);

        uint32_t remaining =
            received_bytes - offset;

        uint32_t chunk =
            (remaining > samples_per_block)
                ? samples_per_block
                : remaining;

        /*
         * Convert:
         * unsigned 8-bit PCM
         * ->
         * signed 16-bit PCM
         */
        for (uint32_t i = 0; i < chunk; i++)
        {
            samples[i] =
                ((int16_t)
                     audio_buffer[offset + i] -
                 128)
                << 8;
        }

        for (uint32_t i = chunk;
             i < samples_per_block;
             i++)
        {
            samples[i] = 0;
        }

        ret = i2s_write(
            i2s_dev,
            block,
            I2S_BLOCK_SIZE);

        if (ret)
        {
            LOG_ERR("I2S write failed");

            return;
        }

        offset += chunk;
    }

    i2s_trigger(
        i2s_dev,
        I2S_DIR_TX,
        I2S_TRIGGER_START);

    // LOG_INF("Playback complete");
}
/* ------------------------------------------------ */

int main(void)
{
    if (lora_init() != 0)
    {
        LOG_ERR("LoRa init failed");

        return -1;
    }
    if (i2s_init() != 0)
    {
        LOG_ERR("I2S init failed");

        return -1;
    }

    LOG_INF("========== LoRa RX START ==========");

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

        uint8_t rx_buffer[128];

        memset(rx_buffer, 0,
               sizeof(rx_buffer));

        llcc68_read_data(
            rx_buffer,
            len);

        process_packet(
            rx_buffer,
            len);
        if (received_bytes >= AUDIO_PAYLOAD_SIZE)
        {
            // play_audio();
        }
    }

    return 0;
}
