#include "llcc68_driver.h"
#include <zephyr/pm/device.h>

// //  LOG_MODULE_DECLARE(main, //  LOG_LEVEL_DBG);

#define LORA_DEVICE DT_NODELABEL(sx1262)
#define SPI_OP SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE | SPI_FRAME_FORMAT_MOTOROLA | SPI_TRANSFER_MSB
#define LORA_SPI_DELAY 10

struct spi_config spi_cfg = {
	.frequency = DT_PROP(LORA_DEVICE, spi_max_frequency),
	.operation = SPI_OP,
	.slave = DT_REG_ADDR(LORA_DEVICE),
	.cs.gpio = SPI_CS_GPIOS_DT_SPEC_GET(LORA_DEVICE),
	.cs.delay = LORA_SPI_DELAY};

static const struct device *spi_dev = DEVICE_DT_GET(DT_BUS(LORA_DEVICE));
static const struct gpio_dt_spec lora_busy = GPIO_DT_SPEC_INST_GET(0, busy_gpios);

uint8_t _sf = 8;
uint32_t _bw = 125000;
uint8_t _cr = 5;
bool _ldro;
uint8_t _headerType = LLCC68_HEADER_EXPLICIT;
uint16_t _preambleLength = 12;
uint8_t _payloadLength;
bool _crcType;
bool _invertIq;
uint8_t _payloadTxRx = 0;
uint8_t _bufferIndex = 0;
uint8_t _statusWait;
volatile static uint16_t _statusIrq;
uint32_t _transmitTime = 0;

void peripherals_ready()
{
	int err;
	if (!device_is_ready(spi_dev))
	{
		//  //  LOG_ERR("SPI_DEV not ready!");
		return;
	}
	if (!gpio_is_ready_dt(&lora_busy))
	{
		//  //  LOG_ERR("BUSY pin not ready");
		return;
	}
	err = gpio_pin_configure_dt(&lora_busy, GPIO_INPUT);
	if (err)
	{
		//  //  LOG_ERR("Unable to configure gpio err: %d", err);
		return;
	}
}
void spi_resume(void)
{
	pm_device_action_run(spi_dev, PM_DEVICE_ACTION_RESUME);
	printk("SPI0 is resumed and ready to use\n");
}
void spi_suspend(void)
{
	pm_device_action_run(spi_dev, PM_DEVICE_ACTION_SUSPEND);
	printk("SPI0 is suspended\n");
}
bool lora_busy_check(const struct gpio_dt_spec *spec, uint32_t timeout)
{
	uint32_t t = k_uptime_get_32();
	while (gpio_pin_get_dt(spec) == 1)
	{
		if ((k_uptime_get_32() - t) > timeout)
		{
			return true;
		}
	}
	return false;
}

int llcc68_transfer_read(uint8_t opcode, uint8_t *data, uint8_t data_len)
{
	if (lora_busy_check(&lora_busy, 5000))
		return false;
	int err;

	const struct spi_buf tx_bufs[] = {
		{.buf = &opcode,
		 .len = sizeof(opcode)},
		{.buf = data,
		 .len = data_len}};
	const struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs)};
	const struct spi_buf rx_bufs[] = {
		{.buf = NULL, .len = 1},
		{.buf = data, .len = data_len}};
	const struct spi_buf_set rx = {
		.buffers = rx_bufs,
		.count = ARRAY_SIZE(rx_bufs)};

	err = spi_transceive(spi_dev, &spi_cfg, &tx, &rx);
	if (err)
	{
		return err;
	}
	return 0;
}

int llcc68_transfer_write(uint8_t opcode, uint8_t *data, uint8_t data_len)
{
	if (lora_busy_check(&lora_busy, 5000))
		return false;
	int err;

	const struct spi_buf tx_bufs[] = {
		{.buf = &opcode,
		 .len = 1},
		{.buf = data,
		 .len = data_len}};
	const struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs)};

	err = spi_write(spi_dev, &spi_cfg, &tx);
	if (err)
	{
		return err;
	}

	return 0;
}

void llcc68_write_register(uint8_t *address, size_t address_len, uint8_t *data, uint8_t data_len)
{
	if (lora_busy_check(&lora_busy, 5000))
		return;
	int err;
	uint8_t opcode = LLCC68_WRITE_REGISTER;

	const struct spi_buf tx_bufs[] = {
		{.buf = &opcode,
		 .len = 1},
		{.buf = address,
		 .len = address_len},
		{.buf = data,
		 .len = data_len}};
	const struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs)};

	err = spi_write(spi_dev, &spi_cfg, &tx);
	if (err)
	{
		return;
	}
	return;
}

void llcc68_read_register(uint8_t *address, size_t address_len, uint8_t *data, size_t data_len)
{
	if (lora_busy_check(&lora_busy, 5000))
		return;
	int err;
	uint8_t opcode = LLCC68_READ_REGISTER;
	uint8_t NOP = 0x00;

	const struct spi_buf tx_bufs[] = {
		{.buf = &opcode,
		 .len = 1},
		{.buf = address,
		 .len = address_len},
		{.buf = &NOP,
		 .len = 1},
		{.buf = &NOP,
		 .len = 1}};
	const struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs)};
	const struct spi_buf rx_bufs[] = {
		{.buf = NULL, .len = 1},
		{.buf = data, .len = data_len}};
	const struct spi_buf_set rx = {
		.buffers = rx_bufs,
		.count = ARRAY_SIZE(rx_bufs)};

	err = spi_transceive(spi_dev, &spi_cfg, &tx, &rx);
	if (err)
	{
		return;
	}
	return;
}

void llcc68_write_buffer(uint8_t offset, uint8_t *data, uint8_t data_len)
{
	uint8_t buf_len = data_len + 1;
	uint8_t buf[buf_len];
	buf[0] = offset;
	for (uint8_t i = 0; i < data_len; i++)
	{
		buf[i + 1] = data[i];
	}
	int err = llcc68_transfer_write(LLCC68_WRITE_BUFFER, buf, buf_len);
	if (err)
	{
		//  //  LOG_ERR("Write buffer failed! err: %d going to while loop", err);
		return;
	}
}

void llcc68_read_buffer(uint8_t offset, uint8_t *data, uint8_t data_len)
{
	uint8_t buf_len = data_len + 2;
	uint8_t buf[buf_len];
	memset(buf, 0x00, buf_len);
	buf[0] = offset;

	int err = llcc68_transfer_read(LLCC68_READ_BUFFER, buf, buf_len);
	if (err)
	{
		//  //  LOG_ERR("Read buffer failed! err: %d going to while loop", err);
		return;
	}
	for (uint8_t i = 0; i < data_len; i++)
	{
		data[i] = buf[i + 2];
	}
}

void llcc68_reset(const struct gpio_dt_spec *spec)
{
	if (!gpio_is_ready_dt(spec))
	{
		//  //  LOG_ERR("Reset pin not ready");
		return;
	}
	int err;
	err = gpio_pin_configure_dt(spec, GPIO_OUTPUT_ACTIVE);
	if (err)
	{
		//  //  LOG_ERR("Coudn't configure reset pin");
		return;
	}
	gpio_pin_set_dt(spec, 0);
	k_sleep(K_MSEC(1));
	gpio_pin_set_dt(spec, 1);
	k_sleep(K_MSEC(5));
}

void llcc68_setSleep(uint8_t sleepconfig)
{
	int err = llcc68_transfer_write(LLCC68_OP_SETSLEEP, &sleepconfig, 1);
	if (err)
	{
		//  //  LOG_ERR("Set Sleep failed err: %d", err);
		return;
	}
}

void llcc68_setStandby(uint8_t standbyconfig)
{
	int err = llcc68_transfer_write(LLCC68_OP_SETSTDBY, &standbyconfig, 1);
	if (err)
	{
		//  //  LOG_ERR("Set Standby failed err: %d", err);
		return;
	}
}

void llcc68_setFs()
{
	int err = llcc68_transfer_write(LLCC68_OP_SETFS, NULL, 1);
	if (err)
	{
		//  LOG_ERR("Set Fs failed err: %d", err);
		return;
	}
}

void llcc68_setTx(uint32_t timeout)
{
	uint8_t buf[3];
	buf[0] = timeout >> 16;
	buf[1] = timeout >> 8;
	buf[2] = timeout >> 0;
	int err = llcc68_transfer_write(LLCC68_OP_SETTX, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set Tx failed err: %d", err);
		return;
	}
}

void llcc68_setRx(uint32_t timeout)
{
	uint8_t buf[3];
	buf[0] = timeout >> 16;
	buf[1] = timeout >> 8;
	buf[2] = timeout >> 0;
	int err = llcc68_transfer_write(LLCC68_OP_SETRX, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set Rx failed err: %d", err);
		return;
	}
}

void llcc68_stopTimerOnPreamble(uint8_t enable)
{
	int err = llcc68_transfer_write(LLCC68_OP_STOPTIMERONPRMBLE, &enable, 1);
	if (err)
	{
		//  LOG_ERR("Stop timer on preamble command failed err: %d", err);
		return;
	}
}

void llcc68_setRxDutyCycle(uint32_t rxPeriod, uint32_t sleepPeriod)
{
	uint8_t buf[6];
	buf[0] = rxPeriod >> 16;
	buf[1] = rxPeriod >> 8;
	buf[2] = rxPeriod >> 0;
	buf[3] = sleepPeriod >> 16;
	buf[4] = sleepPeriod >> 8;
	buf[5] = sleepPeriod >> 0;
	int err = llcc68_transfer_write(LLCC68_OP_SETRXDUTYCYCLE, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set Rx Duty cycle command failed err: %d", err);
		return;
	}
}

void llcc68_setCad()
{
	int err = llcc68_transfer_write(LLCC68_OP_SETCAD, NULL, 1);
	if (err)
	{
		//  LOG_ERR("Set Cad command failed err: %d", err);
		return;
	}
}

void llcc68_setTxContinuousWave()
{
	int err = llcc68_transfer_write(LLCC68_OP_SETTXCONTWAVE, NULL, 1);
	if (err)
	{
		//  LOG_ERR("Set tx continuous wave command failed err: %d", err);
		return;
	}
}

void llcc68_setTxInfinitePreamble()
{
	int err = llcc68_transfer_write(LLCC68_OP_SETTXINFPRMBLE, NULL, 1);
	if (err)
	{
		//  LOG_ERR("Set Tx infinite preamble command failed err: %d", err);
		return;
	}
}

void llcc68_setRegulatorMode(uint8_t modeparam)
{
	int err = llcc68_transfer_write(LLCC68_OP_SETREGMODE, &modeparam, 1);
	if (err)
	{
		//  LOG_ERR("Set Regulator mode command failed err: %d", err);
		return;
	}
}

void llcc68_calibrate(uint8_t calibparam)
{
	int err = llcc68_transfer_write(LLCC68_OP_CALIBRATE, &calibparam, 1);
	if (err)
	{
		//  LOG_ERR("Calibrate command failed err: %d", err);
		return;
	}
}

void llcc68_calibrateImage(uint8_t freq1, uint8_t freq2)
{
	uint8_t buf[2];
	buf[0] = freq1;
	buf[1] = freq2;
	int err = llcc68_transfer_write(LLCC68_OP_CALIBIMG, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Calibrate image command failed err: %d", err);
		return;
	}
}

void llcc68_setPaConfig(uint8_t paDutyCycle, uint8_t hpMax, uint8_t deviceSel, uint8_t paLut)
{
	uint8_t buf[4];
	buf[0] = paDutyCycle;
	buf[1] = hpMax;
	buf[2] = deviceSel;
	buf[3] = paLut;
	int err = llcc68_transfer_write(LLCC68_OP_SETPACONF, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set pa config command failed err: %d", err);
		return;
	}
}

void llcc68_setDioIrqParams(uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask)
{
	uint8_t buf[8];
	buf[0] = irqMask >> 8;
	buf[1] = irqMask >> 0;
	buf[2] = dio1Mask >> 8;
	buf[3] = dio1Mask >> 0;
	buf[4] = dio2Mask >> 8;
	buf[5] = dio2Mask >> 0;
	buf[6] = dio3Mask >> 8;
	buf[7] = dio3Mask >> 0;
	int err = llcc68_transfer_write(LLCC68_SETDIOIRQPARAMS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set dio irq params command failed err: %d", err);
		return;
	}
}

void llcc68_getIrqStatus(uint16_t *irqStatus)
{
	uint8_t buf[3] = {0};
	int err = llcc68_transfer_read(LLCC68_GETIRQSTATUS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Get IRQ status command failed err: %d", err);
		return;
	}
	*irqStatus = ((buf[1] >> 8) | buf[2]);
}

void llcc68_clearIrqStatus(uint16_t clearIrqParam)
{
	uint8_t buf[2];
	buf[0] = clearIrqParam >> 8;
	buf[1] = clearIrqParam >> 0;
	int err = llcc68_transfer_write(LLCC68_CLEARIRQSTATUS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Clear irq status command failed err: %d", err);
		return;
	}
}

void llcc68_setDio2AsRfSwitchCtrl(uint8_t enable)
{
	int err = llcc68_transfer_write(LLCC68_SETDIO2ASRFSWITCHCTRL, &enable, 1);
	if (err)
	{
		//  LOG_ERR("Set dio as rf switch control command failed err: %d", err);
		return;
	}
}

void llcc68_setDio3asTcxoCtrl(uint8_t tcxoVoltage, uint32_t delay)
{
	uint8_t buf[4];
	buf[0] = tcxoVoltage;
	buf[1] = (uint8_t)delay >> 16;
	buf[2] = (uint8_t)delay >> 8;
	buf[3] = (uint8_t)delay >> 0;
	int err = llcc68_transfer_write(LLCC68_SETDIO3ASTCXOCTRL, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set dio3 as tcxo control command failed err: %d", err);
		return;
	}
}

void llcc68_setXtalCap(uint8_t xtalA, uint8_t xtalB)
{
	llcc68_setStandby(LLCC68_STDBY_XOSC);
	uint8_t buf[2] = {xtalA, xtalB};
	uint8_t addr[2];
	addr[0] = (uint8_t)(LLCC68_XTA_TRIM >> 8);
	addr[1] = (uint8_t)(LLCC68_XTA_TRIM >> 0);
	llcc68_write_register(addr, sizeof(addr), buf, sizeof(buf));
	llcc68_setStandby(LLCC68_STDBY_RC);
	llcc68_calibrate(0xFF);
}

void llcc68_setRfFrequency(uint32_t rfFrequency)
{
	uint8_t buf[4];
	buf[0] = rfFrequency >> 24;
	buf[1] = rfFrequency >> 16;
	buf[2] = rfFrequency >> 8;
	buf[3] = rfFrequency >> 0;
	int err = llcc68_transfer_write(LLCC68_SETRFFREQ, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set RF frequency command failed err: %d", err);
		return;
	}
}

void llcc68_setPacketType(uint8_t packetType)
{
	int err = llcc68_transfer_write(LLCC68_SETPACKETTYPE, &packetType, 1);
	if (err)
	{
		//  LOG_ERR("Set packet type command failed err: %d", err);
		return;
	}
}

void llcc68_getPacketType(uint8_t *packetType)
{
	uint8_t buf[2];
	memset(buf, 0x00, sizeof(buf));
	int err = llcc68_transfer_read(LLCC68_GETPACKETTYPE, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Get packet type command failed err: %d", err);
		return;
	}
	*packetType = buf[1];
}

void llcc68_setTxParams(uint8_t power, uint8_t rampTime)
{
	uint8_t buf[2];
	buf[0] = power;
	buf[1] = rampTime;
	int err = llcc68_transfer_write(LLCC68_SETTXPARAMS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set TX params command failed err: %d", err);
		return;
	}
}

void llcc68_setModulationParamsLoRa(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro)
{
	uint8_t buf[8];
	memset(&buf, 0x00, 8);
	buf[0] = sf;
	buf[1] = bw;
	buf[2] = cr;
	buf[3] = ldro;
	int err = llcc68_transfer_write(LLCC68_SETMODULATIONPARAMS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("set modulation params lora command failed err: %d", err);
		return;
	}
}

void llcc68_setModulationParamsFSK(uint32_t br, uint8_t pulseShape, uint8_t bandwidth, uint32_t Fdev)
{
	uint8_t buf[8];
	memset(&buf, 0x00, 8);
	buf[0] = (uint8_t)br >> 16;
	buf[1] = (uint8_t)br >> 8;
	buf[2] = (uint8_t)br >> 0;
	buf[3] = pulseShape;
	buf[4] = bandwidth;
	buf[5] = (uint8_t)Fdev >> 16;
	buf[6] = (uint8_t)Fdev >> 8;
	buf[7] = (uint8_t)Fdev >> 0;
	int err = llcc68_transfer_write(LLCC68_SETMODULATIONPARAMS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set modulation params fsk command failed err: %d", err);
		return;
	}
}

void llcc68_setPacketParamsLoRa(uint16_t preambleLength, uint8_t headerType, uint8_t payloadLength, uint8_t crcType, uint8_t invertIq)
{
	uint8_t buf[9];
	memset(&buf, 0x00, 9);
	buf[0] = preambleLength >> 8;
	buf[1] = preambleLength >> 0;
	buf[2] = headerType;
	buf[3] = payloadLength;
	buf[4] = crcType;
	buf[5] = invertIq;
	int err = llcc68_transfer_write(LLCC68_SETPACKETPARAMS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set packet params lora command failed err: %d", err);
		return;
	}
}

void llcc68_setPacketParamsFSK(uint16_t preambleLength, uint8_t preambleDetector, uint8_t syncWordLength, uint8_t addrComp, uint8_t packetType, uint8_t payloadLength, uint8_t crcType, uint8_t whitening)
{
	uint8_t buf[9];
	memset(&buf, 0x00, 9);
	buf[0] = (uint8_t)preambleLength >> 8;
	buf[1] = (uint8_t)preambleLength >> 0;
	buf[2] = preambleDetector;
	buf[3] = syncWordLength;
	buf[4] = addrComp;
	buf[5] = packetType;
	buf[6] = payloadLength;
	buf[7] = crcType;
	buf[8] = whitening;
	int err = llcc68_transfer_write(LLCC68_SETPACKETPARAMS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set packet params fsk command failed err: %d", err);
		return;
	}
}

void llcc68_setCadParams(uint8_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin, uint8_t cadExitMode, uint32_t cadTimeout)
{
	uint8_t buf[7];
	memset(&buf, 0x00, 7);
	buf[0] = cadSymbolNum;
	buf[1] = cadDetPeak;
	buf[2] = cadDetMin;
	buf[3] = cadExitMode;
	buf[4] = (uint8_t)cadTimeout >> 16;
	buf[5] = (uint8_t)cadTimeout >> 8;
	buf[6] = (uint8_t)cadTimeout >> 0;
	int err = llcc68_transfer_write(LLCC68_SETCADPARAMS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set cad params command failed err: %d", err);
		return;
	}
}

void llcc68_setBufferBaseAddress(uint8_t txBaseAddress, uint8_t rxBaseAddress)
{
	uint8_t buf[2];
	buf[0] = txBaseAddress;
	buf[1] = rxBaseAddress;
	int err = llcc68_transfer_write(LLCC68_SETBUFFERBASEADDR, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Set buffer base address command failed err: %d", err);
		return;
	}
}

void llcc68_setLoRaSymbNumTimeout(uint8_t symbnum)
{
	int err = llcc68_transfer_write(LLCC68_SETLORASYMBLNUMTIMEOUT, &symbnum, 1);
	if (err)
	{
		//  LOG_ERR("Set lora symbol number timeout command failed err: %d", err);
		return;
	}
}

uint8_t llcc68_getStatus(void)
{
	uint8_t buf[2];
	int err = llcc68_transfer_read(LLCC68_GETSTATUS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Get status command failed err: %d", err);
		return;
	}
	return buf[1];
}

void llcc68_getRxBufferStatus(uint8_t *payloadLengthRx, uint8_t *rxStartBufferPointer)
{
	uint8_t buf[3];
	memset(buf, 0x00, sizeof(buf));
	int err = llcc68_transfer_read(LLCC68_GETRXBUFFERSTATUS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("get RX buffer status command failed err: %d", err);
		return;
	}
	*payloadLengthRx = buf[1];
	*rxStartBufferPointer = buf[2];
}

void llcc68_getPacketStatus(uint8_t *rssiPkt, uint8_t *snrPkt, uint8_t *signalRssiPkt)
{
	uint8_t buf[4];
	memset(buf, 0x00, sizeof(buf));
	int err = llcc68_transfer_read(LLCC68_GETPACKETSTATUS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Get pakcet status command failed err: %d", err);
		return;
	}
	*rssiPkt = buf[1];
	*snrPkt = buf[2];
	*signalRssiPkt = buf[3];
}

void llcc68_getRssiInst(uint8_t *rssiInst)
{
	uint8_t buf[2];
	memset(buf, 0x00, sizeof(buf));
	int err = llcc68_transfer_read(LLCC68_GETRSSIINST, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Get RSSI inst command failed err: %d", err);
		return;
	}
	*rssiInst = buf[1];
}

void llcc68_getStats(uint16_t *nbPktReceived, uint16_t *nbPktCrcError, uint16_t *nbPktHeaderErr)
{
	uint8_t buf[7];
	memset(buf, 0x00, sizeof(buf));
	int err = llcc68_transfer_read(LLCC68_GET_STATS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Get stats command failed err: %d", err);
		return;
	}
	*nbPktReceived = (buf[1] >> 8) | buf[2];
	*nbPktCrcError = (buf[3] >> 8) | buf[4];
	*nbPktHeaderErr = (buf[5] >> 8) | buf[6];
}

void llcc68_resetStats()
{
	uint8_t buf[6];
	memset(buf, 0x00, sizeof(buf));
	int err = llcc68_transfer_write(LLCC68_RESETSTATS, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Reset stats command failed err: %d", err);
		return;
	}
}

void llcc68_getDeviceErrors(uint16_t *opError)
{
	uint8_t buf[3];
	memset(buf, 0x00, sizeof(buf));
	int err = llcc68_transfer_read(LLCC68_GETDEVICEERR, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Get device errors command failed err: %d", err);
		return;
	}
	*opError = buf[2];
}

void llcc68_clearDeviceErrors()
{
	uint8_t buf[2];
	memset(buf, 0x00, sizeof(buf));
	int err = llcc68_transfer_write(LLCC68_CLEARDEVICEERR, buf, sizeof(buf));
	if (err)
	{
		//  LOG_ERR("Clear device error command failed err: %d", err);
		return;
	}
}

void llcc68_fixLoRaBw500(uint32_t bw)
{
	uint8_t packetType;
	llcc68_getPacketType(&packetType);
	uint8_t value[4] = {0};
	uint8_t addr[2];
	addr[0] = (uint8_t)(LLCC68_TX_MODULATION >> 8);
	addr[1] = (uint8_t)(LLCC68_TX_MODULATION >> 0);
	llcc68_read_register(addr, sizeof(addr), &value, 4);
	if ((packetType == LLCC68_PACKETTYPE_LORA) && (bw == 500000))
	{
		value[3] &= 0xFB;
	}
	else
	{
		value[3] |= 0x04;
	}
	llcc68_write_register(addr, sizeof(addr), &value[3], 1);
}

void llcc68_fixResistanceAntenna()
{
	uint8_t value[4];
	uint8_t addr[2];
	addr[0] = (uint8_t *)(LLCC68_TX_CLAMP_CONFIG >> 8);
	addr[1] = (uint8_t *)(LLCC68_TX_CLAMP_CONFIG >> 0);
	llcc68_read_register(addr, sizeof(addr), &value, sizeof(value));
	value[3] |= 0x1E;
	llcc68_write_register(addr, sizeof(addr), &value[3], 1);
}

void llcc68_fixRxTimeout()
{
	uint8_t value[4] = {0};
	memset(value, 0x00, sizeof(value));
	uint8_t addr[2];
	addr[0] = (LLCC68_RTC_CONTROL >> 8);
	addr[1] = (LLCC68_RTC_CONTROL >> 0);
	uint8_t addr_2nd[2];
	addr_2nd[0] = (LLCC68_EVENTMASK >> 8);
	addr_2nd[1] = (LLCC68_EVENTMASK >> 0);
	llcc68_write_register(addr, sizeof(addr), &value, 1);
	llcc68_read_register(addr_2nd, sizeof(addr_2nd), &value, sizeof(value));
	value[3] |= 0x02;
	llcc68_write_register(addr_2nd, sizeof(addr_2nd), &value[3], 1);
}

void llcc68_fixInvertedIq(uint8_t invertIq)
{
	uint8_t value[4] = {0};
	uint8_t addr[2];
	addr[0] = (LLCC68_IQ_POLARITY_SETUP >> 8);
	addr[1] = (LLCC68_IQ_POLARITY_SETUP >> 0);
	llcc68_read_register(addr, sizeof(addr), &value, 4);
	if (invertIq)
	{
		value[3] |= 0x04;
	}
	else
	{
		value[3] &= 0xFB;
	}
	llcc68_write_register(addr, sizeof(addr), &value[3], 1);
}

uint8_t llcc68_getMode()
{
	uint8_t mode;
	mode = llcc68_getStatus();
	return mode & 0x70;
}

void llcc68_setFrequency(uint32_t frequency)
{
	uint8_t calFreq[2];
	if (frequency < 446000000)
	{ // 430 - 440 Mhz
		calFreq[0] = LLCC68_CAL_IMG_430;
		calFreq[1] = LLCC68_CAL_IMG_440;
	}
	else if (frequency < 734000000)
	{ // 470 - 510 Mhz
		calFreq[0] = LLCC68_CAL_IMG_470;
		calFreq[1] = LLCC68_CAL_IMG_510;
	}
	else if (frequency < 828000000)
	{ // 779 - 787 Mhz
		calFreq[0] = LLCC68_CAL_IMG_779;
		calFreq[1] = LLCC68_CAL_IMG_787;
	}
	else if (frequency < 877000000)
	{ // 863 - 870 Mhz
		calFreq[0] = LLCC68_CAL_IMG_863;
		calFreq[1] = LLCC68_CAL_IMG_870;
	}
	else if (frequency < 1100000000)
	{ // 902 - 928 Mhz
		calFreq[0] = LLCC68_CAL_IMG_902;
		calFreq[1] = LLCC68_CAL_IMG_928;
	}
	// calculate frequency for setting configuration
	uint32_t rfFreq = ((uint64_t)frequency << LLCC68_RF_FREQUENCY_SHIFT) / LLCC68_RF_FREQUENCY_XTAL;
	llcc68_calibrateImage(calFreq[0], calFreq[1]);
	llcc68_setRfFrequency(rfFreq);
}

void llcc68_setTxPower(uint8_t txPower, uint8_t version)
{
	// maximum TX power is 22 dBm and 15 dBm for SX1261
	if (txPower > 22)
		txPower = 22;
	// else if (txPower > 15 && version == SX126X_TX_POWER_SX1261)
	// 	txPower = 15;
	else if (txPower < -9)
		txPower = -9;

	uint8_t paDutyCycle = 0x00;
	uint8_t hpMax = 0x00;
	uint8_t deviceSel = version == SX126X_TX_POWER_SX1261 ? 0x01 : 0x00;
	// set parameters for PA config and TX params configuration
	if (txPower == 22)
	{
		paDutyCycle = 0x04;
		hpMax = 0x07;
	}
	else if (txPower >= 20)
	{
		paDutyCycle = 0x03;
		hpMax = 0x05;
	}
	else if (txPower >= 17)
	{
		paDutyCycle = 0x02;
		hpMax = 0x03;
	}
	else if (txPower >= 14)
	{
		paDutyCycle = 0x02;
		hpMax = 0x02;
	}
	else if (txPower < 14)
	{
		paDutyCycle = 0x02;
		hpMax = 0x02;
	}

	llcc68_setPaConfig(paDutyCycle, hpMax, deviceSel, 0x01);
	llcc68_setTxParams(txPower, LLCC68_PA_RAMP_800U);
}

void llcc68_LoraModulation(uint8_t sf, uint32_t bw, uint8_t cr, bool ldro)
{
	_sf = sf;
	_bw = bw;
	_cr = cr;
	_ldro = ldro;

	// valid spreading factor is between 5 and 12
	if (sf > 12)
		sf = 12;
	else if (sf < 5)
		sf = 5;
	// select bandwidth options
	if (bw < 9100)
		bw = LLCC68_BW_7800; // 7.8 kHz
	else if (bw < 13000)
		bw = LLCC68_BW_10400; // 10.4 kHz
	else if (bw < 18200)
		bw = LLCC68_BW_15600; // 15.6 kHz
	else if (bw < 26000)
		bw = LLCC68_BW_20800; // 20.8 kHz
	else if (bw < 36500)
		bw = LLCC68_BW_31250; // 31.25 kHz
	else if (bw < 52100)
		bw = LLCC68_BW_41700; // 41.7 kHz
	else if (bw < 93800)
		bw = LLCC68_BW_62500; // 62.5 kHz
	else if (bw < 187500)
		bw = LLCC68_BW_125000; // 125 kHz
	else if (bw < 375000)
		bw = LLCC68_BW_250000; // 250 kHz
	else
		bw = LLCC68_BW_500000; // 500 kHz
	// valid code rate denominator is between 4 and 8
	cr -= 4;
	if (cr > 4)
		cr = 0;

	llcc68_setModulationParamsLoRa(sf, (uint8_t)bw, cr, (uint8_t)ldro);
}

void llcc68_setLoraPacket(uint8_t headerType, uint16_t preambleLength, uint8_t payloadLength, bool crcType, bool invertIq)
{
	_headerType = headerType;
	_preambleLength = preambleLength;
	_payloadLength = payloadLength;
	_crcType = crcType;
	_invertIq = invertIq;

	// filter valid header type config
	if (headerType != LLCC68_HEADER_IMPLICIT)
		headerType = LLCC68_HEADER_EXPLICIT;
	llcc68_setPacketParamsLoRa(preambleLength, headerType, payloadLength, (uint8_t)crcType, (uint8_t)invertIq);
	llcc68_fixInvertedIq((uint8_t)invertIq);
}

void llcc68_setSyncWord(uint16_t syncWord)
{
	uint8_t buf[2];
	buf[0] = syncWord >> 8;
	buf[1] = syncWord & 0xFF;
	if (syncWord <= 0xFF)
	{
		buf[0] = (syncWord & 0xF0) | 0x04;
		buf[1] = (syncWord << 4) | 0x04;
	}
	uint8_t addr[2];
	addr[0] = (LLCC68_LORA_SYNC_WORD_MSB >> 8);
	addr[1] = (LLCC68_LORA_SYNC_WORD_MSB >> 0);
	llcc68_write_register(addr, sizeof(addr), buf, sizeof(buf));
}

void llcc68_beginPacket()
{
	// reset payload length and buffer index
	_payloadTxRx = 0;
	llcc68_setBufferBaseAddress(_bufferIndex, _bufferIndex + 0xFF);
	// gpio_pin_set_dt(&rx_en_pin, HIGH);
	// gpio_pin_set_dt(&tx_en_pin, LOW);
	llcc68_fixLoRaBw500(_bw);
}

void llcc68_write_char(char *data, uint8_t length)
{
	uint8_t *data_ = (uint8_t *)data;
	llcc68_write_buffer(_bufferIndex, data_, length);
	_bufferIndex += length;
	_payloadTxRx += length;
}

void llcc68_write_data(uint8_t *data, uint8_t length)
{
	llcc68_write_buffer(_bufferIndex, data, length);
	_bufferIndex += length;
	_payloadTxRx += length;
}

bool llcc68_endPacket(uint32_t timeout)
{
	if (llcc68_getMode() == LLCC68_STATUS_MODE_TX)
	{
		return false;
	}

	llcc68_irqSetup(LLCC68_IRQ_TX_DONE | LLCC68_IRQ_TIMEOUT);
	llcc68_setLoraPacket(_headerType, _preambleLength, _payloadTxRx, _crcType, _invertIq);

	_statusWait = LLCC68_STATUS_TX_WAIT;
	_statusIrq = 0x0000;

	uint32_t txTimeout = timeout << 6;

	if (txTimeout > 0x00FFFFFF)
	{
		txTimeout = LLCC68_TX_SINGLE;
	}

	llcc68_setTx(txTimeout);
	_transmitTime = k_uptime_get_32();

	return true;
}

bool llcc68_wait(uint32_t timeout)
{
	if (_statusIrq)
	{
		return true;
	}

	uint16_t irqStat = 0x0000;
	uint32_t t = k_uptime_get_32();

	while (irqStat == 0x0000 && _statusIrq == 0x0000)
	{
		llcc68_getIrqStatus(&irqStat);
		if (k_uptime_get_32() - t > timeout && timeout != 0)
		{
			return false;
		}
		k_yield();
	}

	if (_statusIrq)
	{
		// immediately return when interrupt signal hit
		return true;
	}
	else if (_statusWait == LLCC68_STATUS_TX_WAIT)
	{
		// for transmit, calculate transmit time and set back txen pin to low
		_transmitTime = k_uptime_get_32() - _transmitTime;
		// gpio_pin_set_dt(&tx_en_pin, HIGH);
	}
	else if (_statusWait == LLCC68_STATUS_RX_WAIT)
	{
		// for receive, get received payload length and buffer index and set back rxen pin to low
		llcc68_getRxBufferStatus(&_payloadTxRx, &_bufferIndex);
		// gpio_pin_set_dt(&rx_en_pin, HIGH);
		llcc68_fixRxTimeout();
	}
	else if (_statusWait == LLCC68_STATUS_RX_CONTINUOUS)
	{
		// for receive continuous, get received payload length and buffer index and clear IRQ status
		llcc68_getRxBufferStatus(&_payloadTxRx, &_bufferIndex);
		llcc68_clearIrqStatus(0x03FF);
	}

	// store IRQ status
	_statusIrq = irqStat;
	return true;
}

void llcc68_irqSetup(uint16_t irqMask)
{
	llcc68_clearIrqStatus(0x03FFF);

	uint16_t dio1Mask = 0x0000;
	uint16_t dio2Mask = 0x0000;
	uint16_t dio3Mask = 0x0000;

	/**
	 * @attention: Add block to set other DIOs as IRQpin
	 */
	dio1Mask = irqMask;
	llcc68_setDioIrqParams(irqMask, dio1Mask, dio2Mask, dio3Mask);
}

uint32_t llcc68_transmitTime()
{
	return _transmitTime;
}

bool llcc68_request(uint32_t timeout)
{
	if (llcc68_getMode() == LLCC68_STATUS_MODE_RX)
	{
		return false;
	}
	llcc68_irqSetup(LLCC68_IRQ_RX_DONE | LLCC68_IRQ_TIMEOUT | LLCC68_IRQ_HEADER_ERR | LLCC68_IRQ_CRC_ERR);
	_statusWait = LLCC68_STATUS_RX_WAIT;
	_statusIrq = 0x0000;

	uint32_t rxTimeout = timeout << 6;
	if (rxTimeout > 0x00FFFFFF)
	{
		rxTimeout = LLCC68_RX_SINGLE;
	}
	if (timeout == LLCC68_RX_CONTINUOUS)
	{
		rxTimeout = LLCC68_RX_CONTINUOUS;
		_statusWait = LLCC68_STATUS_RX_CONTINUOUS;
	}

	// gpio_pin_set_dt(&rx_en_pin, LOW);
	// gpio_pin_set_dt(&tx_en_pin, HIGH);
	llcc68_setRx(rxTimeout);
	return true;
}

bool llcc68_listen(uint32_t rxPeriod, uint32_t sleepPeriod)
{
	if (llcc68_getMode() == LLCC68_STATUS_MODE_RX)
	{
		return false;
	}
	llcc68_irqSetup(LLCC68_IRQ_RX_DONE | LLCC68_IRQ_TIMEOUT | LLCC68_IRQ_HEADER_ERR | LLCC68_IRQ_CRC_ERR);
	_statusWait = LLCC68_STATUS_RX_WAIT;
	_statusIrq = 0x0000;

	rxPeriod = rxPeriod << 6;
	sleepPeriod = sleepPeriod << 6;
	if (rxPeriod > 0x00FFFFFF)
		rxPeriod = 0x00FFFFFF;
	if (sleepPeriod > 0x00FFFFFF)
		sleepPeriod = 0x00FFFFFF;

	// gpio_pin_set_dt(&rx_en_pin, LOW);
	// gpio_pin_set_dt(&tx_en_pin, HIGH);

	llcc68_setRxDutyCycle(rxPeriod, sleepPeriod);
	return true;
}

uint8_t llcc68_available()
{
	return _payloadTxRx;
}

uint8_t llcc68_read()
{
	uint8_t buf[2];
	llcc68_read_buffer(_bufferIndex, buf, sizeof(buf));
	_bufferIndex++;
	if (_payloadTxRx > 0)
		_payloadTxRx--;
	return buf[1];
}

uint8_t llcc68_read_char(char *data, uint8_t length)
{
	uint8_t *data_ = (uint8_t *)data;
	llcc68_read_buffer(_bufferIndex, data_, length);
	_bufferIndex += length;
	_payloadTxRx = _payloadTxRx > length ? _payloadTxRx - length : 0;
	return _payloadTxRx > length ? length : _payloadTxRx;
}

uint8_t llcc68_read_data(uint8_t *data, uint8_t length)
{
	// read multiple bytes of received package
	llcc68_read_buffer(_bufferIndex, data, length);
	// return smallest between read length and size of package available
	_bufferIndex += length;
	_payloadTxRx = _payloadTxRx > length ? _payloadTxRx - length : 0;
	return _payloadTxRx > length ? length : _payloadTxRx;
}

uint8_t llcc68_read_data_number()
{
	uint8_t buf[5];
	memset(buf, 0x00, sizeof(buf));
	llcc68_read_buffer(_bufferIndex, buf, sizeof(buf));
	_bufferIndex++;
	if (_payloadTxRx > 0)
		_payloadTxRx--;
	return buf[1];
}

void llcc68_purge(uint8_t length)
{
	_payloadTxRx = (_payloadTxRx > length) && length ? _payloadTxRx - length : 0;
	_bufferIndex += length;
}

uint8_t llcc68_status()
{
	uint16_t statusIrq = _statusIrq;
	if (_statusWait == LLCC68_STATUS_RX_CONTINUOUS)
	{
		_statusIrq = 0x0000;
	}

	// get status for transmit and receive operation based on status IRQ
	if (statusIrq & LLCC68_IRQ_TIMEOUT)
	{
		if (_statusWait == LLCC68_STATUS_TX_WAIT)
			return LLCC68_STATUS_TX_TIMEOUT;
		else
			return LLCC68_STATUS_RX_TIMEOUT;
	}
	else if (statusIrq & LLCC68_IRQ_HEADER_ERR)
		return LLCC68_STATUS_HEADER_ERR;
	else if (statusIrq & LLCC68_IRQ_CRC_ERR)
		return LLCC68_STATUS_CRC_ERR;
	else if (statusIrq & LLCC68_IRQ_TX_DONE)
		return LLCC68_STATUS_TX_DONE;
	else if (statusIrq & LLCC68_IRQ_RX_DONE)
		return LLCC68_STATUS_RX_DONE;

	// return TX or RX wait status
	return _statusWait;
}

float llcc68_dataRate()
{
	// get data rate last transmitted package in kbps
	return 1000.0 * _payloadTxRx / _transmitTime;
}

int16_t llcc68_packetRssi()
{
	// get relative signal strength index (RSSI) of last incoming package
	uint8_t rssiPkt, snrPkt, signalRssiPkt;
	llcc68_getPacketStatus(&rssiPkt, &snrPkt, &signalRssiPkt);
	return (rssiPkt / -2);
}

float llcc68_snr()
{
	// get signal to noise ratio (SNR) of last incoming package
	uint8_t rssiPkt, snrPkt, signalRssiPkt;
	llcc68_getPacketStatus(&rssiPkt, &snrPkt, &signalRssiPkt);
	return ((int8_t)snrPkt / 4.0);
}

int16_t llcc68_signalRssi()
{
	uint8_t rssiPkt, snrPkt, signalRssiPkt;
	llcc68_getPacketStatus(&rssiPkt, &snrPkt, &signalRssiPkt);
	return (signalRssiPkt / -2);
}

int16_t llcc68_rssiInst()
{
	uint8_t rssiInst;
	llcc68_getRssiInst(&rssiInst);
	return (rssiInst / -2);
}

uint16_t llcc68_getError()
{
	uint16_t error;
	llcc68_getDeviceErrors(&error);
	llcc68_clearDeviceErrors();
	return error;
}

void llcc68_setRxGain(uint8_t boost)
{
	// set power saving or boosted gain in register
	uint8_t gain = boost ? LLCC68_BOOSTED_GAIN : LLCC68_POWER_SAVING_GAIN;
	uint8_t addr[2];
	addr[0] = (LLCC68_RX_GAIN >> 8);
	addr[1] = (LLCC68_RX_GAIN >> 0);
	llcc68_write_register(addr, sizeof(addr), &gain, 1);
	if (boost)
	{
		uint8_t addr_2nd[2];
		addr_2nd[0] = (LLCC68_RX_BOOSTED_GAIN_CONF >> 8);
		addr_2nd[1] = (LLCC68_RX_BOOSTED_GAIN_CONF >> 0);
		// set certain register to retain configuration after wake from sleep mode
		uint8_t buf[3] = {0x01, 0x08, 0xAC};
		llcc68_write_register(addr_2nd, sizeof(addr_2nd), buf, sizeof(buf));
	}
}
