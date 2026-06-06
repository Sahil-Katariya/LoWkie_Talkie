#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>

#include "llcc68_driver.h"
#include "audio_data.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_GET(DT_ALIAS(lora0), reset_gpios);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct gpio_callback button_cb_data;

volatile bool transmit_requested = false;

#define AUDIO_PAYLOAD_SIZE 64
#define PACKET_SYNC 0xA55A

typedef struct __packed
{
    uint16_t sync;
    uint16_t packet_id;
    uint16_t payload_size;
    uint8_t payload[AUDIO_PAYLOAD_SIZE];

} audio_packet_t;

static audio_packet_t tx_packet;

void button_pressed(const struct device *dev,
                    struct gpio_callback *cb,
                    uint32_t pins)
{
    transmit_requested = true;

    LOG_INF("Button Pressed");
}

static void button_init(void)
{
    if (!gpio_is_ready_dt(&button))
    {
        LOG_ERR("Button device not ready");
        return;
    }

    gpio_pin_configure_dt(&button, GPIO_INPUT);

    gpio_pin_interrupt_configure_dt(
        &button,
        GPIO_INT_EDGE_TO_ACTIVE);

    gpio_init_callback(
        &button_cb_data,
        button_pressed,
        BIT(button.pin));

    gpio_add_callback(button.port, &button_cb_data);

    LOG_INF("Button initialized");
}

static void lora_configure(void)
{
    LOG_INF("Initializing SX1262");

    peripherals_ready();
    llcc68_reset(&reset_pin);
    llcc68_setStandby(LLCC68_STDBY_RC);
    uint8_t mode = llcc68_getMode();
    // LOG_INF("Mode read = 0x%02X", mode);
    if (mode != LLCC68_STATUS_MODE_STDBY_RC)
    {
        LOG_WRN("Wrong status received! (mode: 0x%X)", mode);
    }

    llcc68_setPacketType(LLCC68_PACKETTYPE_LORA);
    llcc68_fixResistanceAntenna();
    llcc68_setDio2AsRfSwitchCtrl(1);
    uint8_t xtalA = 0x12;
    uint8_t xtalB = 0x12;
    llcc68_setXtalCap(xtalA, xtalB);

    llcc68_setFrequency(865000000);
    llcc68_setTxPower(22, SX126X_TX_POWER_SX1262);

    uint8_t sf = 9;
    uint32_t bw = 250000;
    uint8_t cr = 1;
    llcc68_LoraModulation(sf, bw, cr, false);

    uint8_t headerType = LLCC68_HEADER_EXPLICIT;
    uint16_t preambleLength = 8;
    uint8_t payloadLength = sizeof(audio_packet_t);
    bool crcType = true;
    llcc68_setLoraPacket(headerType, preambleLength, payloadLength, crcType, false);

    llcc68_setSyncWord(0x3444);
    llcc68_setDio2AsRfSwitchCtrl(true);
    LOG_INF("SX1262 Ready");
}

static void send_audio(void)
{
    LOG_INF("========== AUDIO TRANSMIT START ==========");

    uint32_t offset = 0;
    uint16_t packet_id = 0;

    while (offset < audio_len)
    {
        memset(&tx_packet, 0, sizeof(tx_packet));

        tx_packet.sync = PACKET_SYNC;

        tx_packet.packet_id = packet_id;

        uint32_t remaining = audio_len - offset;

        uint16_t chunk_size =
            (remaining > AUDIO_PAYLOAD_SIZE)
                ? AUDIO_PAYLOAD_SIZE
                : remaining;

        tx_packet.payload_size = chunk_size;

        memcpy(
            tx_packet.payload,
            &audio[offset],
            chunk_size);

        llcc68_beginPacket();

        uint16_t tx_size =
            sizeof(tx_packet.sync) +
            sizeof(tx_packet.packet_id) +
            sizeof(tx_packet.payload_size) +
            chunk_size;

        llcc68_write_data(
            (uint8_t *)&tx_packet,
            tx_size);

        llcc68_endPacket(0U);

        llcc68_wait(0U);

        uint32_t tx_time = llcc68_transmitTime();

        LOG_INF(
            "Packet %d sent | Size=%d | TX=%d ms",
            packet_id,
            chunk_size,
            tx_time);

        offset += chunk_size;

        packet_id++;

        uint8_t status = llcc68_status();
        if (status == LLCC68_STATUS_TX_DONE)
        {
            LOG_INF("TxDone");
        }

        k_sleep(K_MSEC(10));
    }

    LOG_INF("Audio transmission completed");
}

int main(void)
{
    LOG_INF("========== LORA AUDIO TRANSMITTER START ==========");

    button_init();

    lora_configure();

    LOG_INF("Audio Size = %d bytes", audio_len);

    while (1)
    {
        if (transmit_requested)
        {
            LOG_INF("Starting Transmission");

            transmit_requested = false;

            send_audio();
        }

        k_sleep(K_MSEC(10));
    }

    return 0;
}
