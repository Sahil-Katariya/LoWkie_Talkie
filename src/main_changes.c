
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include "llcc68_driver.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

LOG_MODULE_REGISTER(lora_llcc68, LOG_LEVEL_INF);
static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_INST_GET(0, reset_gpios);

#define AUDIO_PAYLOAD_SIZE 200
#define MAX_AUDIO_SIZE 16384

#define RX_TIMEOUT_MS 5000

char message[] = "transmitting LoRa Packet";
uint8_t nBytes = sizeof(message);
uint8_t counter = 0;

/* ---------------- Packet Structure ---------------- */

typedef struct __packed
{
    uint16_t packet_id;
    uint16_t payload_len;

    uint8_t is_last;
    uint8_t data[AUDIO_PAYLOAD_SIZE];

} lora_audio_packet_t;

/* ---------------- Global Audio Buffer ---------------- */

static uint8_t audio_buffer[MAX_AUDIO_SIZE];
static uint32_t received_bytes = 0;
static uint16_t expected_packet = 0;
static uint32_t total_packets = 0;

static int lora_init(void)
{
    peripherals_ready();
    LOG_INF("Starting LoWkie RX!");
    llcc68_reset(&reset_pin);
    llcc68_setStandby(LLCC68_STDBY_RC);
    uint8_t mode = llcc68_getMode();
    if (mode != LLCC68_STATUS_MODE_STDBY_RC)
    {
        LOG_WRN("Wrong status received! (mode: 0x%X)", mode);
        return -1;
    }
    llcc68_setPacketType(LLCC68_PACKETTYPE_LORA);
    llcc68_fixResistanceAntenna();

    uint8_t xtalA = 0x12;
    uint8_t xtalB = 0x12;
    llcc68_setXtalCap(xtalA, xtalB);

    llcc68_setFrequency(865100000);
    llcc68_setTxPower(22, SX126X_TX_POWER_SX1262);

    uint8_t sf = 9;
    uint32_t bw = 250000;
    uint8_t cr = 1;
    llcc68_LoraModulation(sf, bw, cr, true);

    uint8_t headerType = LLCC68_HEADER_EXPLICIT;
    uint16_t preambleLength = 8;
    uint8_t payloadLength = sizeof(lora_audio_packet_t);
    bool crcType = true;
    llcc68_setLoraPacket(headerType, preambleLength, payloadLength, crcType, false);

    llcc68_setSyncWord(0x3444);
    LOG_INF("LoWkie RX Initialized");
    return 0;
}

static void process_packet(uint8_t *rx_data, uint8_t len)
{
    if (len < sizeof(lora_audio_packet_t))
    {
        LOG_WRN("Packet too small");
        return;
    }

    lora_audio_packet_t *pkt =
        (lora_audio_packet_t *)rx_data;

    /* ---------------- Sequence Validation ---------------- */
    if (pkt->packet_id != expected_packet)
    {
        LOG_ERR(
            "Packet mismatch! Expected %u got %u",
            expected_packet,
            pkt->packet_id);

        return;
    }

    /* ---------------- Buffer Overflow Check ---------------- */
    if ((received_bytes + pkt->payload_len) >
        MAX_AUDIO_SIZE)
    {
        LOG_ERR("Audio buffer overflow");

        return;
    }

    /* ---------------- Copy Payload ---------------- */
    memcpy(
        &audio_buffer[received_bytes],
        pkt->data,
        pkt->payload_len);

    received_bytes += pkt->payload_len;
    expected_packet++;
    total_packets++;

    int16_t rssi = llcc68_packetRssi();
    int8_t snr = (int8_t)llcc68_snr();

    LOG_INF(
        "RX Packet %u | %u bytes | RSSI=%d | SNR=%d",
        pkt->packet_id,
        pkt->payload_len,
        rssi,
        snr);

    /* ---------------- Transmission Complete ---------------- */
    if (pkt->is_last)
    {
        LOG_INF("Audio Reception Complete");
        LOG_INF("Total Bytes: %u", received_bytes);
        LOG_INF("Total Packets: %u", total_packets);

        /*
         * - save to SD card
         * - playback using I2S/DAC
         */
    }
}

int main(void)
{
    if (lora_init() != 0)
    {
        LOG_ERR("LoRa init failed");
        return -1;
    }

    LOG_INF("Starting LoWkie RX");

    while (1)
    {
        llcc68_request(0U);
        llcc68_wait(0U);
        uint8_t len = llcc68_available();
        if (len == 0)
        {
            continue;
        }

        uint8_t rx_buffer[sizeof(lora_audio_packet_t)];
        memset(rx_buffer, 0, sizeof(rx_buffer));
        llcc68_read_data(rx_buffer, len);
        printk("%d. Message: %s | Length: %u bytes\n", counter++, rx_buffer, sizeof(rx_buffer));
        process_packet(rx_buffer, len);
        int16_t rssi = llcc68_packetRssi();
        int8_t snr = (int8_t)llcc68_snr();
        printk("%d. Message: %s | RSSI: %d dBm | SNR: %d | Length: %u bytes\n", counter++, rx_buffer, rssi, snr, sizeof(rx_buffer));
    }

    return 0;
}
