#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdint.h>

/* Speex instead of Codec2 */
#include <speex/speex.h>

#include "packet.h"
#include "llcc68_driver.h"
#include "audio_data.h"

LOG_MODULE_REGISTER(lowkie_talkie, LOG_LEVEL_INF);

#define USE_TEST_AUDIO 1 // 1: Stored PCM, 0: Live Mic

#define JITTER_BUFFER_TARGET_PACKETS 5 // 800ms buffer

/* Speex Timing Updates */
#define SAMPLES_PER_FRAME 160 // 20ms @ 8kHz for Speex Narrowband
#define FRAMES_PER_PACKET 8   // 8 frames * 20ms = 160ms audio window

/* Memory Slabs and Message Queues */
K_MSGQ_DEFINE(tx_audio_msgq, SAMPLES_PER_FRAME * sizeof(int16_t), 10, 4);
K_MSGQ_DEFINE(tx_lora_msgq, sizeof(lowkie_packet_t), 10, 4);

K_MSGQ_DEFINE(rx_lora_msgq, sizeof(lowkie_packet_t), 20, 4);
K_MSGQ_DEFINE(rx_audio_msgq, SAMPLES_PER_FRAME * sizeof(int16_t) * FRAMES_PER_PACKET, 10, 4);

static volatile bool is_ptt_pressed = false;

/* Hardware Specs */
static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_INST_GET(0, reset_gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb_data;

/* -------------------------------------------------------------------------- */
/* HARDWARE INITIALIZATION                                                    */
/* -------------------------------------------------------------------------- */

void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
        int val = gpio_pin_get_dt(&button);
        is_ptt_pressed = (val > 0);

        if (is_ptt_pressed)
        {
                LOG_INF("PTT Pressed - Entering TX Mode");
        }
        else
        {
                LOG_INF("PTT Released - Entering RX Mode");
        }
}

static void button_init(void)
{
        if (!gpio_is_ready_dt(&button))
        {
                LOG_ERR("Button device not ready");
                return;
        }

        gpio_pin_configure_dt(&button, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
        gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(button.pin));
        gpio_add_callback(button.port, &button_cb_data);

        LOG_INF("PTT Button initialized");
}

void lora_init(void)
{
        peripherals_ready();
        LOG_INF("Starting LORA SX126X!");

        llcc68_reset(&reset_pin);
        llcc68_setStandby(LLCC68_STDBY_RC);

        uint8_t mode = llcc68_getMode();
        if (mode != LLCC68_STATUS_MODE_STDBY_RC)
        {
                LOG_WRN("Wrong status received! (mode: 0x%X)", mode);
                return;
        }

        llcc68_setPacketType(LLCC68_PACKETTYPE_LORA);
        llcc68_fixResistanceAntenna();

        LOG_INF("Set RF module to use XTAL as clock reference");
        llcc68_setXtalCap(0x12, 0x12);

        LOG_INF("Setting LORA Frequency!");
        llcc68_setFrequency(865100000);

        LOG_INF("Setting TX power");
        llcc68_setTxPower(14, SX126X_TX_POWER_SX1262);

        LOG_INF("Setting modulation parameters");
        uint8_t sf = 7;
        uint32_t bw = 250000;
        uint8_t cr = 1;
        llcc68_LoraModulation(sf, bw, cr, true);

        LOG_INF("Setting Packet Parameters");
        uint8_t headerType = LLCC68_HEADER_EXPLICIT;
        uint16_t preambleLength = 16;
        uint8_t payloadLength = sizeof(lowkie_packet_t);
        bool crcType = true;
        llcc68_setLoraPacket(headerType, preambleLength, payloadLength, crcType, false);

        LOG_INF("Setting sync word");
        llcc68_setSyncWord(PACKET_SYNC_WORD);
}

/* -------------------------------------------------------------------------- */
/* AUDIO & CODEC PIPELINE THREADS (SPEEX INTEGER API)                         */
/* -------------------------------------------------------------------------- */

void tx_audio_capture_thread(void)
{
        int16_t pcm_buffer[SAMPLES_PER_FRAME];
        uint32_t phase = 0;

        while (1)
        {
                if (!is_ptt_pressed)
                {
                        k_msleep(50); // Yield CPU when not transmitting
                        continue;
                }

#if USE_TEST_AUDIO
                for (int i = 0; i < SAMPLES_PER_FRAME; i++)
                {
                        pcm_buffer[i] = (phase++ % 18) > 9 ? 8000 : -8000;
                }
                k_msleep(20); // 20ms yield to match Speex frame length
#else
                // i2s_read(i2s_dev, &pcm_buffer, sizeof(pcm_buffer), &bytes_read);
#endif
                k_msgq_put(&tx_audio_msgq, &pcm_buffer, K_NO_WAIT);
        }
}

void tx_speex_encoder_thread(void)
{
        void *speex_enc = speex_encoder_init(&speex_nb_mode);
        if (speex_enc == NULL)
        {
                LOG_ERR("Failed to create Speex instance for TX");
                return;
        }

        // Force Quality 1 (~2.15 kbps) to ensure payload fits inside LoRa limits
        int quality = 1;
        speex_encoder_ctl(speex_enc, SPEEX_SET_QUALITY, &quality);

        SpeexBits bits;
        speex_bits_init(&bits);

        int16_t pcm_buffer[SAMPLES_PER_FRAME];
        lowkie_packet_t packet;
        uint16_t seq_id = 0;
        int frame_idx = 0;

        while (1)
        {
                k_msgq_get(&tx_audio_msgq, &pcm_buffer, K_FOREVER);

                if (frame_idx == 0)
                {
                        packet.sync_word = PACKET_SYNC_WORD;
                        packet.packet_id = seq_id++;
                        packet.timestamp = k_uptime_get_32();
                        packet.codec_mode = CODEC_MODE_SPEEX_2150; // Defined in packet.h
                        speex_bits_reset(&bits);
                }

                // Integer-only Speex encoding
                speex_encode_int(speex_enc, pcm_buffer, &bits);
                frame_idx++;

                if (frame_idx == FRAMES_PER_PACKET)
                {
                        // Write compressed bits to payload array
                        packet.payload_size = speex_bits_write(&bits, packet.payload, sizeof(packet.payload));
                        packet.crc16 = 0xFFFF; // Replace with actual CRC calculation
                        k_msgq_put(&tx_lora_msgq, &packet, K_NO_WAIT);
                        frame_idx = 0;
                }
        }

        // Clean up (though this infinite loop never reaches here)
        speex_bits_destroy(&bits);
        speex_encoder_destroy(speex_enc);
}

void rx_jitter_buffer_thread(void)
{
        lowkie_packet_t packet;
        lowkie_packet_t jbuf[20];
        int jbuf_count = 0;
        bool is_playing = false;
        uint16_t expected_id = 0;

        void *speex_dec = speex_decoder_init(&speex_nb_mode);
        if (speex_dec == NULL)
        {
                LOG_ERR("Failed to create Speex instance for RX");
                return;
        }

        SpeexBits bits;
        speex_bits_init(&bits);

        int16_t pcm_out[SAMPLES_PER_FRAME * FRAMES_PER_PACKET];

        while (1)
        {
                if (!is_playing)
                {
                        k_msgq_get(&rx_lora_msgq, &packet, K_FOREVER);
                        jbuf[jbuf_count++] = packet;

                        if (jbuf_count >= JITTER_BUFFER_TARGET_PACKETS)
                        {
                                is_playing = true;
                                expected_id = jbuf[0].packet_id;
                        }
                }
                else
                {
                        int16_t diff = (int16_t)(jbuf[0].packet_id - expected_id);

                        if (diff < 0)
                        {
                                LOG_WRN("Discarding old packet %d (expected %d)", jbuf[0].packet_id, expected_id);
                                memmove(&jbuf[0], &jbuf[1], sizeof(lowkie_packet_t) * (jbuf_count - 1));
                                jbuf_count--;
                                if (jbuf_count == 0)
                                {
                                        is_playing = false;
                                }
                                continue;
                        }
                        else if (diff > 0)
                        {
                                LOG_WRN("Missing packet %d, inserting silence", expected_id);
                                memset(pcm_out, 0, sizeof(pcm_out));
                                expected_id++;
                                k_msgq_put(&rx_audio_msgq, pcm_out, K_NO_WAIT);

                                if (k_msgq_get(&rx_lora_msgq, &packet, K_NO_WAIT) == 0)
                                {
                                        jbuf[jbuf_count++] = packet;
                                }
                                continue;
                        }

                        packet = jbuf[0];
                        memmove(&jbuf[0], &jbuf[1], sizeof(lowkie_packet_t) * (jbuf_count - 1));
                        jbuf_count--;
                        expected_id++;

                        // Load bits from payload and decode
                        speex_bits_read_from(&bits, packet.payload, packet.payload_size);

                        for (int i = 0; i < FRAMES_PER_PACKET; i++)
                        {
                                // Integer-only Speex decoding
                                speex_decode_int(speex_dec, &bits, &pcm_out[i * SAMPLES_PER_FRAME]);
                        }

                        k_msgq_put(&rx_audio_msgq, pcm_out, K_NO_WAIT);

                        if (k_msgq_get(&rx_lora_msgq, &packet, K_NO_WAIT) == 0)
                        {
                                jbuf[jbuf_count++] = packet;
                        }

                        if (jbuf_count == 0)
                        {
                                LOG_WRN("Jitter buffer empty, forcing rebuffering");
                                is_playing = false;
                        }
                }
        }

        speex_bits_destroy(&bits);
        speex_decoder_destroy(speex_dec);
}

void rx_audio_play_thread(void)
{
        int16_t pcm_out[SAMPLES_PER_FRAME * FRAMES_PER_PACKET];
        while (1)
        {
                k_msgq_get(&rx_audio_msgq, pcm_out, K_FOREVER);
                // i2s_write(i2s_dev, pcm_out, sizeof(pcm_out), &bytes_written);
        }
}

/* -------------------------------------------------------------------------- */
/* UNIFIED TRANSCEIVER THREAD (Half-Duplex Master)                            */
/* -------------------------------------------------------------------------- */

void lora_transceiver_thread(void)
{
        lowkie_packet_t packet;

        while (1)
        {
                if (is_ptt_pressed)
                {
                        /* --- TX MODE --- */
                        if (k_msgq_get(&tx_lora_msgq, &packet, K_NO_WAIT) == 0)
                        {
                                llcc68_beginPacket();
                                llcc68_write_data((uint8_t *)&packet, sizeof(packet));
                                llcc68_endPacket(0U);

                                llcc68_wait(500U);

                                uint8_t status_temp = llcc68_status();
                                if (status_temp == LLCC68_STATUS_TX_DONE)
                                {
                                        LOG_DBG("Transmitted Packet ID: %d", packet.packet_id);
                                }
                        }
                        else
                        {
                                k_msleep(10);
                        }
                }
                else
                {
                        /* --- RX MODE --- */
                        llcc68_request(100U);
                        llcc68_wait(100U);

                        uint8_t dataLen = llcc68_available();
                        if (dataLen == sizeof(lowkie_packet_t))
                        {
                                llcc68_read_data((uint8_t *)&packet, dataLen);

                                if (packet.sync_word == PACKET_SYNC_WORD)
                                {
                                        int16_t rssi = llcc68_packetRssi();
                                        int8_t snr = (int8_t)llcc68_snr();
                                        LOG_INF("Packet %d Received | RSSI: %d dBm | SNR: %d", packet.packet_id, rssi, snr);

                                        k_msgq_put(&rx_lora_msgq, &packet, K_NO_WAIT);
                                }
                        }
                }
        }
}

/* -------------------------------------------------------------------------- */
/* THREAD REGISTRATION                                                        */
/* -------------------------------------------------------------------------- */

// RX audio playback gets highest priority to avoid stutter
K_THREAD_DEFINE(rx_audio_id, 2048, rx_audio_play_thread, NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(lora_trans_id, 2048, lora_transceiver_thread, NULL, NULL, NULL, 2, 0, 0);

// Notice K_FP_REGS has been removed (set to 0) since Speex runs purely on integer math
K_THREAD_DEFINE(rx_codec_id, 4096, rx_jitter_buffer_thread, NULL, NULL, NULL, 3, 0, 0);
K_THREAD_DEFINE(tx_codec_id, 4096, tx_speex_encoder_thread, NULL, NULL, NULL, 4, 0, 0);
K_THREAD_DEFINE(tx_audio_id, 2048, tx_audio_capture_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
        LOG_INF("========== LoWkie Talkie Transceiver Starting ==========");

        button_init();
        lora_init();

        while (1)
        {
                k_sleep(K_SECONDS(1));
        }
}