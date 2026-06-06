#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include "llcc68_driver.h"
#include <zephyr/pm/device.h>
#include <hal/nrf_power.h>

// LOG_MODULE_REGISTER(lora_llcc68, LOG_LEVEL_INF);
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_GET(DT_ALIAS(resetpin), gpios);
static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_GET(DT_ALIAS(lora0), reset_gpios);

char message[] = "HW=ello world, this is a text message from the lora module, sent via UARt and transmitted over lora, and received back from the lora receiver";
uint8_t nBytes = sizeof(message);
uint8_t counter = 0;

int main(void)
{
        // begin
        peripherals_ready();
        LOG_INF("Starting LORA SX126X!");
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
        LOG_INF("Set RF module to use XTAL as clock reference");
        llcc68_setXtalCap(xtalA, xtalB);

        LOG_INF("Setting LORA Frequency!");
        llcc68_setFrequency(865100000);
        LOG_INF("Setting TX power");
        llcc68_setTxPower(22, SX126X_TX_POWER_SX1262);

        LOG_INF("Setting modulation parameters");
        uint8_t sf = 7;
        uint32_t bw = 250000;
        uint8_t cr = 1;
        llcc68_LoraModulation(sf, bw, cr, true);

        LOG_INF("Setting Packet Parameters");
        uint8_t headerType = LLCC68_HEADER_EXPLICIT;
        uint16_t preambleLength = 16;
        uint8_t payloadLength = 64;
        bool crcType = true;
        llcc68_setLoraPacket(headerType, preambleLength, payloadLength, crcType, false);

        LOG_INF("Setting sync word");
        llcc68_setSyncWord(0x3444);
        llcc68_setDio2AsRfSwitchCtrl(true);
        LOG_INF("=====================LORA TRANSMITTER=====================");

        while (1)
        {
                // k_sleep(K_MSEC(4400));
                llcc68_beginPacket();
                llcc68_write_char(message, nBytes);
                llcc68_write_data(&counter, 1);
                llcc68_endPacket(0U);

                LOG_INF("%s %d", message, counter);
                counter++;
                llcc68_wait(0U);
                uint32_t time = llcc68_transmitTime();
                LOG_INF("Transmit Time: %d ms", time);
                uint8_t status = llcc68_status();
                if (status == LLCC68_STATUS_TX_DONE)
                {
                        LOG_INF("TxDone");
                }

                k_sleep(K_MSEC(100));
        }
}