#ifndef _LORA_DRIVER_LLCC68_
#define _LORA_DRIVER_LLCC68_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
// Make LoRa GPIO specs available
extern const struct gpio_dt_spec lora_busy;
extern uint8_t __tx_power;
#define DT_DRV_COMPAT semtech_sx1262
// Operation Mode Opcodes

#define LLCC68_OP_SETSLEEP              0x84
#define LLCC68_OP_SETSTDBY              0x80
#define LLCC68_OP_SETFS                 0xC1
#define LLCC68_OP_SETTX                 0x83
#define LLCC68_OP_SETRX                 0x82
#define LLCC68_OP_STOPTIMERONPRMBLE     0x9F
#define LLCC68_OP_SETRXDUTYCYCLE        0x94
#define LLCC68_OP_SETCAD                0xC5
#define LLCC68_OP_SETTXCONTWAVE         0xD1
#define LLCC68_OP_SETTXINFPRMBLE        0xD2
#define LLCC68_OP_SETREGMODE            0x96
#define LLCC68_OP_CALIBRATE             0x89
#define LLCC68_OP_CALIBIMG              0x98
#define LLCC68_OP_SETPACONF             0x95
#define LLCC68_OP_RXTXFALLBACKMODE      0x93

// Register and Buffer Access Opcode

#define LLCC68_WRITE_REGISTER           0x0D
#define LLCC68_READ_REGISTER            0x1D
#define LLCC68_WRITE_BUFFER             0x0E
#define LLCC68_READ_BUFFER              0x1E

// DIO and IRQ Controll Opcode

#define LLCC68_SETDIOIRQPARAMS          0x08
#define LLCC68_GETIRQSTATUS             0x12
#define LLCC68_CLEARIRQSTATUS           0x02
#define LLCC68_SETDIO2ASRFSWITCHCTRL    0x9D
#define LLCC68_SETDIO3ASTCXOCTRL        0x97

// RF Modulation and Packet Commands

#define LLCC68_SETRFFREQ                0x86
#define LLCC68_SETPACKETTYPE            0x8A
#define LLCC68_GETPACKETTYPE            0x11
#define LLCC68_SETTXPARAMS              0x8E
#define LLCC68_SETMODULATIONPARAMS      0x8B
#define LLCC68_SETPACKETPARAMS          0x8C
#define LLCC68_SETCADPARAMS             0x88
#define LLCC68_SETBUFFERBASEADDR        0x8F
#define LLCC68_SETLORASYMBLNUMTIMEOUT   0xA0

// Status Commands

#define LLCC68_GETSTATUS                0xC0
#define LLCC68_GETRSSIINST              0x15
#define LLCC68_GETRXBUFFERSTATUS        0x13
#define LLCC68_GETPACKETSTATUS          0x14
#define LLCC68_GETDEVICEERR             0x17
#define LLCC68_CLEARDEVICEERR           0x07
#define LLCC68_GET_STATS                0x10
#define LLCC68_RESETSTATS               0x00

// Register map (FSK MODEM Commands not included)

#define LLCC68_DIOX_OUTPUT_ENABLE       0x0580
#define LLCC68_DIOX_INPUT_ENABLE        0x0583
#define LLCC68_DIOX_PULLUP_CTRL         0x0584
#define LLCC68_DIOX_PULL_DOWN_CTRL      0x0585
#define LLCC68_IQ_POLARITY_SETUP        0x0736
#define LLCC68_LORA_SYNC_WORD_MSB       0x0740
#define LLCC68_LORA_SYNC_WORD_LSB       0x0741
#define LLCC68_TX_MODULATION            0x0889
#define LLCC68_RX_GAIN                  0x08AC
#define LLCC68_TX_CLAMP_CONFIG          0x08D8
#define LLCC68_OCP_CONF                 0x08E7
#define LLCC68_RTC_CONTROL              0x0902
#define LLCC68_XTA_TRIM                 0x0911  // Refer Datasheet before using this
#define LLCC68_XTB_TRIM                 0x0912  // Refer Datasheet before using this
#define LLCC68_DIO3_OUT_VOLT_CTRL       0x0920
#define LLCC68_EVENTMASK                0x0944

// Standby modes

#define LLCC68_STDBY_RC                 0x00
#define LLCC68_STDBY_XOSC               0x01

// Modem type

#define LLCC68_PACKETTYPE_GFSK          0x00
#define LLCC68_PACKETTYPE_LORA          0x01

// Get Status

#define LLCC68_STATUS_DATA_AVAILABLE            0x04        // command status: packet received and data can be retrieved
#define LLCC68_STATUS_CMD_TIMEOUT               0x06        //                 SPI command timed out
#define LLCC68_STATUS_CMD_ERROR                 0x08        //                 invalid SPI command
#define LLCC68_STATUS_CMD_FAILED                0x0A        //                 SPI command failed to execute
#define LLCC68_STATUS_CMD_TX_DONE               0x0C        //                 packet transmission done
#define LLCC68_STATUS_MODE_STDBY_RC             0x20        // current chip mode: STDBY_RC
#define LLCC68_STATUS_MODE_STDBY_XOSC           0x30        //                    STDBY_XOSC
#define LLCC68_STATUS_MODE_FS                   0x40        //                    FS
#define LLCC68_STATUS_MODE_RX                   0x50        //                    RX
#define LLCC68_STATUS_MODE_TX                   0x60        //                    TX

// SetDio3AsTcxoCtrl

#define LLCC68_DIO3_OUTPUT_1_6                  0x00        // DIO3 voltage output for TCXO: 1.6 V
#define LLCC68_DIO3_OUTPUT_1_7                  0x01        //                               1.7 V
#define LLCC68_DIO3_OUTPUT_1_8                  0x02        //                               1.8 V
#define LLCC68_DIO3_OUTPUT_2_2                  0x03        //                               2.2 V
#define LLCC68_DIO3_OUTPUT_2_4                  0x04        //                               2.4 V
#define LLCC68_DIO3_OUTPUT_2_7                  0x05        //                               2.7 V
#define LLCC68_DIO3_OUTPUT_3_0                  0x06        //                               3.0 V
#define LLCC68_DIO3_OUTPUT_3_3                  0x07        //                               3.3 V
#define LLCC68_TCXO_DELAY_1                     0x0040      // TCXO delay time: 1 ms
#define LLCC68_TCXO_DELAY_2                     0x0080      //                  2 ms
#define LLCC68_TCXO_DELAY_5                     0x0140      //                  5 ms
#define LLCC68_TCXO_DELAY_10                    0x0280      //                  10 ms

// CalibrateImage
#define LLCC68_CAL_IMG_430                      0x6B        // ISM band: 430-440 Mhz Freq1
#define LLCC68_CAL_IMG_440                      0x6F        //           430-440 Mhz Freq2
#define LLCC68_CAL_IMG_470                      0x75        //           470-510 Mhz Freq1
#define LLCC68_CAL_IMG_510                      0x81        //           470-510 Mhz Freq2
#define LLCC68_CAL_IMG_779                      0xC1        //           779-787 Mhz Freq1
#define LLCC68_CAL_IMG_787                      0xC5        //           779-787 Mhz Freq2
#define LLCC68_CAL_IMG_863                      0xD7        //           863-870 Mhz Freq1
#define LLCC68_CAL_IMG_870                      0xDB        //           863-870 Mhz Freq2
#define LLCC68_CAL_IMG_902                      0xE1        //           902-928 Mhz Freq1
#define LLCC68_CAL_IMG_928                      0xE9        //           902-928 Mhz Freq2

#define LLCC68_RF_FREQUENCY_XTAL                32000000    // XTAL frequency used for RF frequency calculation
#define LLCC68_RF_FREQUENCY_SHIFT               25          // RfFreq = Frequency * 2^25 / 32000000

// SetPaConfig
#define SX126X_TX_POWER_SX1261                  0x01        // device version for TX power: SX1261
#define SX126X_TX_POWER_SX1262                  0x02        //                            : SX1262
#define SX126X_TX_POWER_SX1268                  0x08        //                            : SX1268

// SetTxParams
#define LLCC68_PA_RAMP_10U                      0x00        // ramp time: 10 us
#define LLCC68_PA_RAMP_20U                      0x01        //            20 us
#define LLCC68_PA_RAMP_40U                      0x02        //            40 us
#define LLCC68_PA_RAMP_80U                      0x03        //            80 us
#define LLCC68_PA_RAMP_200U                     0x04        //            200 us
#define LLCC68_PA_RAMP_800U                     0x05        //            800 us
#define LLCC68_PA_RAMP_1700U                    0x06        //            1700 us
#define LLCC68_PA_RAMP_3400U                    0x07        //            3400 us


// SetPacketParams for LoRa packet type
#define LLCC68_HEADER_EXPLICIT                  0x00        // LoRa header mode: explicit
#define LLCC68_HEADER_IMPLICIT                  0x01        //                   implicit
#define LLCC68_CRC_OFF                          0x00        // LoRa CRC mode: disabled
#define LLCC68_CRC_ON                           0x01        //                enabled
#define LLCC68_IQ_STANDARD                      0x00        // LoRa IQ setup: standard
#define LLCC68_IQ_INVERTED                      0x01        //                inverted

// SetModulationParams for LoRa packet type
#define LLCC68_BW_7800                          0x00        // LoRa bandwidth: 7.8 kHz
#define LLCC68_BW_10400                         0x08        //                 10.4 kHz
#define LLCC68_BW_15600                         0x01        //                 15.6 kHz
#define LLCC68_BW_20800                         0x09        //                 20.8 kHz
#define LLCC68_BW_31250                         0x02        //                 31.25 kHz
#define LLCC68_BW_41700                         0x0A        //                 41.7 kHz
#define LLCC68_BW_62500                         0x03        //                 62.5 kHz
#define LLCC68_BW_125000                        0x04        //                 125 kHz
#define LLCC68_BW_250000                        0x05        //                 250 kHz
#define LLCC68_BW_500000                        0x06        //                 500 kHz
#define LLCC68_CR_4_4                           0x00        // LoRa coding rate: 4/4 (no coding rate)
#define LLCC68_CR_4_5                           0x01        //                   4/5
#define LLCC68_CR_4_6                           0x02        //                   4/6
#define LLCC68_CR_4_7                           0x03        //                   4/7
#define LLCC68_CR_4_8                           0x04        //                   4/8
#define LLCC68_LDRO_OFF                         0x00        // LoRa low data rate optimization: disabled
#define LLCC68_LDRO_ON                          0x00        //                                  enabled

// SetDioIrqParams
#define LLCC68_IRQ_TX_DONE                      0x0001      // packet transmission completed
#define LLCC68_IRQ_RX_DONE                      0x0002      // packet received
#define LLCC68_IRQ_PREAMBLE_DETECTED            0x0004      // preamble detected
#define LLCC68_IRQ_SYNC_WORD_VALID              0x0008      // valid sync word detected
#define LLCC68_IRQ_HEADER_VALID                 0x0010      // valid LoRa header received
#define LLCC68_IRQ_HEADER_ERR                   0x0020      // LoRa header CRC error
#define LLCC68_IRQ_CRC_ERR                      0x0040      // wrong CRC received
#define LLCC68_IRQ_CAD_DONE                     0x0080      // channel activity detection finished
#define LLCC68_IRQ_CAD_DETECTED                 0x0100      // channel activity detected
#define LLCC68_IRQ_TIMEOUT                      0x0200      // Rx or Tx timeout
#define LLCC68_IRQ_ALL                          0x03FF      // all interrupts
#define LLCC68_IRQ_NONE                         0x0000      // no interrupts

// Status TX and RX operation
#define LLCC68_STATUS_DEFAULT                     0           // default status (false)
#define LLCC68_STATUS_TX_WAIT                     1
#define LLCC68_STATUS_TX_TIMEOUT                  2
#define LLCC68_STATUS_TX_DONE                     3
#define LLCC68_STATUS_RX_WAIT                     4
#define LLCC68_STATUS_RX_CONTINUOUS               5
#define LLCC68_STATUS_RX_TIMEOUT                  6
#define LLCC68_STATUS_RX_DONE                     7
#define LLCC68_STATUS_HEADER_ERR                  8
#define LLCC68_STATUS_CRC_ERR                     9
#define LLCC68_STATUS_CAD_WAIT                    10
#define LLCC68_STATUS_CAD_DETECTED                11
#define LLCC68_STATUS_CAD_DONE                    12

// SetTx
#define LLCC68_TX_SINGLE                        0x000000    // Tx timeout duration: no timeout (Rx single mode)

// SetRx
#define LLCC68_RX_SINGLE                        0x000000    // Rx timeout duration: no timeout (Rx single mode)
#define LLCC68_RX_CONTINUOUS                    0xFFFFFF    //                      infinite (Rx continuous mode)

// RxGain
#define LLCC68_RX_GAIN_POWER_SAVING             0x00        // gain used in Rx mode: power saving gain (default)
#define LLCC68_RX_GAIN_BOOSTED                  0x01        //                       boosted gain
#define LLCC68_POWER_SAVING_GAIN                0x94        // power saving gain register value
#define LLCC68_BOOSTED_GAIN                     0x96        // boosted gain register value

#define LLCC68_RX_BOOSTED_GAIN_CONF             0x029F


#define LOW 0
#define HIGH 1


// Functions
/**
 * @brief Initializes peripherals needs to be called first
*/
void peripherals_ready();
/**
 * @brief Function to write data to register address
 * 
 * @param address Address of register to write (array buffer)
 * @param address_len Length of register address buffer
 * @param data Data to be written to address (array buffer)
 * @param data_len Length of data to be written
*/
void llcc68_write_register(uint8_t *address, size_t address_len, uint8_t *data, uint8_t data_len);
/**
 * @brief Function to read data from register address
 * 
 * @param address register address from which to read (array buffer)
 * @param address_len Length of resister address buffer
 * @param data where the read data will be stored (array buffer)
 * @param data_len Length of the data to be read
*/
void llcc68_read_register(uint8_t *address, size_t address_len, uint8_t *data, size_t data_len);
/**
 * @brief Write data to the LLCC68 buffer starting at a specified offset
 * 
 * @param offset The starting offset in the LLCC68 buffer where the data should be written
 * @param data Pointer to the data to be written to the buffer
 * @param data_len Length of the data to be written in bytes 
*/
void llcc68_write_buffer(uint8_t offset, uint8_t* data, uint8_t data_len);
/**
 * @brief Reads data from the LLCC68 buffer starting from a specified offset
 *
 * @param offset The offset in the LLCC68 buffer from which to start reading
 * @param data Pointer to the buffer where the read data will be stored
 * @param data_len Length of the data to be read in bytes
*/
void llcc68_read_buffer(uint8_t offset, uint8_t* data, uint8_t data_len);
/**
*  @brief Reset the LLCC68 LoRa transceiver using the specified GPIO pin
 *
 * @param spec Pointer to the GPIO device tree specification structure that
 *             defines the reset pin
*/
void llcc68_reset(const struct gpio_dt_spec *spec);
/**
 * @brief Puts the LLCC68 transceiver into sleep mode
 *
 * @param sleepconfig Configuration settings for sleep mode
 */
void llcc68_setSleep(uint8_t sleepconfig);
/**
 * @brief Function to set LLCC68 to Standby mode
 * 
 * @param standbyconfig Config for standby
 */
void llcc68_setStandby(uint8_t standbyconfig);
void llcc68_setFs();
void llcc68_setTx(uint32_t timeout);
void llcc68_setRx(uint32_t timeout);
void llcc68_stopTimerOnPreamble(uint8_t enable);
void llcc68_setRxDutyCycle(uint32_t rxPeriod, uint32_t sleepPeriod);
void llcc68_setCad();
void llcc68_setTxContinuousWave();
void llcc68_setTxInfinitePreamble();
void llcc68_setRegulatorMode(uint8_t modeparam);
void llcc68_calibrate(uint8_t calibparam);
void llcc68_calibrateImage(uint8_t freq1, uint8_t freq2);
void llcc68_setPaConfig(uint8_t paDutyCycle, uint8_t hpMax, uint8_t deviceSel, uint8_t paLut);
void llcc68_setDioIrqParams(uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask);
void llcc68_getIrqStatus(uint16_t* irqStatus);
void llcc68_clearIrqStatus(uint16_t clearIrqParam);
void llcc68_setDio2AsRfSwitchCtrl(uint8_t enable);
void llcc68_setDio3asTcxoCtrl(uint8_t tcxoVoltage, uint32_t delay);
void llcc68_setXtalCap(uint8_t xtalA, uint8_t xtalB);
void llcc68_setRfFrequency(uint32_t rfFrequency);
/**
 * @brief Sets up packet type for Modulation
 * 
 * @param packetType LORA or GFSK
 */
void llcc68_setPacketType(uint8_t packetType);
void llcc68_getPacketType(uint8_t* packetType);
void llcc68_setTxParams(uint8_t power, uint8_t rampTime);
/**
 * @brief Function to set modulation parameter for LoRa
 * 
 * @param sf Spreading factor
 * @param bw Bandwidth
 * @param cr Code Rate
 * @param ldro Low data rate optimization
 */
void llcc68_setModulationParamsLoRa(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro);
void llcc68_setModulationParamsFSK(uint32_t br, uint8_t pulseShape, uint8_t bandwidth, uint32_t Fdev);
/**
 * @brief To set packet parameter of LoRa modulation
 * 
 * @param preambleLength
 * @param headerType
 * @param payloadLength
 * @param crcType
 * @param invertIq
 */
void llcc68_setPacketParamsLoRa(uint16_t preambleLength, uint8_t headerType, uint8_t payloadLength, uint8_t crcType, uint8_t invertIq);
void llcc68_setPacketParamsFSK(uint16_t preambleLength, uint8_t preambleDetector, uint8_t syncWordLength, uint8_t addrComp, uint8_t packetType, uint8_t payloadLength, uint8_t crcType, uint8_t whitening);
void llcc68_setCadParams(uint8_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin, uint8_t cadExitMode, uint32_t cadTimeout);
void llcc68_setBufferBaseAddress(uint8_t txBaseAddress, uint8_t rxBaseAddress);
void llcc68_setLoRaSymbNumTimeout(uint8_t symbnum);
uint8_t llcc68_getStatus(void);
void llcc68_getRxBufferStatus(uint8_t* payloadLengthRx, uint8_t* rxStartBufferPointer);
void llcc68_getPacketStatus(uint8_t* rssiPkt, uint8_t* snrPkt, uint8_t* signalRssiPkt);
void llcc68_getRssiInst(uint8_t* rssiInst);
void llcc68_getStats(uint16_t* nbPktReceived, uint16_t* nbPktCrcError, uint16_t* nbPktHeaderErr);
void llcc68_resetStats();
void llcc68_getDeviceErrors(uint16_t* opError);
void llcc68_clearDeviceErrors();
/**
 * @brief Workaround as mentioned in datasheet
 * 
 * @param bw bandwidth
 */
void llcc68_fixLoRaBw500(uint32_t bw);
/**
 * @brief Workaround as mentioned in datasheet
 */
void llcc68_fixResistanceAntenna();
/**
 * @brief Workaround as mentioned in datasheet
 */
void llcc68_fixRxTimeout();
/**
 * @brief Workaround as mentioned in datasheet
 */
void llcc68_fixInvertedIq(uint8_t invertIq);


uint8_t llcc68_getMode();
void llcc68_setFrequency(uint32_t frequency);
void llcc68_setTxPower(uint8_t txPower, uint8_t version);
void llcc68_LoraModulation(uint8_t sf, uint32_t bw, uint8_t cr, bool ldro);
void llcc68_setLoraPacket(uint8_t headerType, uint16_t preambleLength, uint8_t payloadLength, bool crcType, bool invetIq);
void llcc68_setSyncWord(uint16_t syncWord);

void llcc68_beginPacket();
void llcc68_write_char(char* data, uint8_t length);
void llcc68_write_data(uint8_t* data, uint8_t length);
bool llcc68_endPacket(uint32_t timeout);
bool llcc68_wait(uint32_t timeout);
void llcc68_irqSetup(uint16_t irqMask);
bool llcc68_request(uint32_t timeout);
bool llcc68_listen(uint32_t rxPeriod, uint32_t sleepPeriod);
uint8_t llcc68_available();
uint8_t llcc68_read();
uint8_t llcc68_read_char(char* data, uint8_t length);
uint8_t llcc68_read_data(uint8_t* data, uint8_t length);
void llcc68_purge(uint8_t length);
uint8_t llcc68_status();
float llcc68_dataRate();
int16_t llcc68_packetRssi();
float llcc68_snr();
int16_t llcc68_signalRssi();
int16_t llcc68_rssiInst();
uint16_t llcc68_getError();
uint32_t llcc68_transmitTime();
void llcc68_setRxGain(uint8_t boost);

uint8_t llcc68_read_data_number();

#endif