#include "sx1262_driver.h"

#define radioSPI SPI

// ═════════════════════════════════════════════════════════════════════════════
//  Bandwidth Lookup Table
// ═════════════════════════════════════════════════════════════════════════════
struct BWEntry { uint8_t code; float kHz; const char* label; };

static const BWEntry BW_TABLE[] = {
    { BW_7K8,    7.81f,  "7.81 kHz"  },
    { BW_10K4,  10.42f,  "10.42 kHz" },
    { BW_15K6,  15.63f,  "15.63 kHz" },
    { BW_20K8,  20.83f,  "20.83 kHz" },
    { BW_31K25, 31.25f,  "31.25 kHz" },
    { BW_41K7,  41.67f,  "41.67 kHz" },
    { BW_62K5,  62.50f,  "62.50 kHz" },
    { BW_125K, 125.00f,  "125 kHz"   },
    { BW_250K, 250.00f,  "250 kHz"   },
    { BW_500K, 500.00f,  "500 kHz"   },
};
static const uint8_t BW_TABLE_SIZE = sizeof(BW_TABLE) / sizeof(BW_TABLE[0]);

// ═════════════════════════════════════════════════════════════════════════════
//  Static Bandwidth Utilities
// ═════════════════════════════════════════════════════════════════════════════

uint8_t SX1262Radio::bwFromKHz(float kHz) {
    uint8_t best = BW_125K;
    float   bestDiff = 1e6f;
    for (uint8_t i = 0; i < BW_TABLE_SIZE; i++) {
        float diff = kHz > BW_TABLE[i].kHz
                   ? kHz - BW_TABLE[i].kHz
                   : BW_TABLE[i].kHz - kHz;
        if (diff < bestDiff) { bestDiff = diff; best = BW_TABLE[i].code; }
    }
    return best;
}

const char* SX1262Radio::bwToStr(uint8_t bw) {
    for (uint8_t i = 0; i < BW_TABLE_SIZE; i++)
        if (BW_TABLE[i].code == bw) return BW_TABLE[i].label;
    return "??? kHz";
}

float SX1262Radio::bwToKHz(uint8_t bw) {
    for (uint8_t i = 0; i < BW_TABLE_SIZE; i++)
        if (BW_TABLE[i].code == bw) return BW_TABLE[i].kHz;
    return 125.0f;
}

// ═════════════════════════════════════════════════════════════════════════════
//  HAL  – Hardware Abstraction Layer
// ═════════════════════════════════════════════════════════════════════════════

void SX1262Radio::_waitBusy() {
    uint32_t t0 = millis();
    while (digitalRead(SX1262_BUSY_PIN) == HIGH) {
        delayMicroseconds(100);
        if (millis() - t0 > 2000) {
            Serial1.println("[ERR] BUSY timeout – check wiring / power");
            return;
        }
    }
}

void SX1262Radio::_hardReset() {
    digitalWrite(SX1262_RESET_PIN, LOW);
    delay(100);
    digitalWrite(SX1262_RESET_PIN, HIGH);
    delay(100);
}

void SX1262Radio::_writeCmd(uint8_t cmd, const uint8_t* d, uint8_t n) {
    _waitBusy();
    digitalWrite(SX1262_NSS_PIN, LOW);
    radioSPI.transfer(cmd);
    for (uint8_t i = 0; i < n; i++) radioSPI.transfer(d[i]);
    digitalWrite(SX1262_NSS_PIN, HIGH);
}

void SX1262Radio::_readCmd(uint8_t cmd, uint8_t* d, uint8_t n) {
    _waitBusy();
    digitalWrite(SX1262_NSS_PIN, LOW);
    radioSPI.transfer(cmd);
    radioSPI.transfer(0x00);
    for (uint8_t i = 0; i < n; i++) d[i] = radioSPI.transfer(0x00);
    digitalWrite(SX1262_NSS_PIN, HIGH);
}

void SX1262Radio::_writeReg(uint16_t addr, const uint8_t* d, uint8_t n) {
    _waitBusy();
    digitalWrite(SX1262_NSS_PIN, LOW);
    radioSPI.transfer(CMD_WRITE_REGISTER);
    radioSPI.transfer((uint8_t)(addr >> 8));
    radioSPI.transfer((uint8_t)(addr & 0xFF));
    for (uint8_t i = 0; i < n; i++) radioSPI.transfer(d[i]);
    digitalWrite(SX1262_NSS_PIN, HIGH);
}

void SX1262Radio::_readReg(uint16_t addr, uint8_t* d, uint8_t n) {
    _waitBusy();
    digitalWrite(SX1262_NSS_PIN, LOW);
    radioSPI.transfer(CMD_READ_REGISTER);
    radioSPI.transfer((uint8_t)(addr >> 8));
    radioSPI.transfer((uint8_t)(addr & 0xFF));
    radioSPI.transfer(0x00);
    for (uint8_t i = 0; i < n; i++) d[i] = radioSPI.transfer(0x00);
    digitalWrite(SX1262_NSS_PIN, HIGH);
}

void SX1262Radio::_txSwitch() {
    digitalWrite(SX1262_RXEN_PIN, LOW);
    digitalWrite(SX1262_TXEN_PIN, HIGH);
}

void SX1262Radio::_rxSwitch() {
    digitalWrite(SX1262_TXEN_PIN, LOW);
    digitalWrite(SX1262_RXEN_PIN, HIGH);
}

void SX1262Radio::_rfOff() {
    digitalWrite(SX1262_TXEN_PIN, LOW);
    digitalWrite(SX1262_RXEN_PIN, LOW);
}

void SX1262Radio::_clearIrq() {
    uint8_t buf[2] = { 0xFF, 0xFF };
    _writeCmd(CMD_CLEAR_IRQ_STATUS, buf, 2);
}

uint16_t SX1262Radio::_getIrqStatus() {
    uint8_t buf[2] = {};
    _readCmd(CMD_GET_IRQ_STATUS, buf, 2);
    return ((uint16_t)buf[0] << 8) | buf[1];
}

// ═════════════════════════════════════════════════════════════════════════════
//  Internal Config Helpers
// ═════════════════════════════════════════════════════════════════════════════

void SX1262Radio::_applyModulation() {
    if (_cfg.modem == MODEM_GFSK) {
        // GFSK modulation params
        uint32_t brReg = (uint32_t)(1024000000ULL / (_cfg.gfskBitrate ? _cfg.gfskBitrate : 50000));
        uint32_t fdevReg = (uint32_t)((double)_cfg.gfskFdev * 33554432.0 / 32000000.0);
        uint8_t buf[8] = {
            (uint8_t)(brReg >> 16),
            (uint8_t)(brReg >> 8),
            (uint8_t)(brReg),
            _cfg.gfskPulseShape,
            _cfg.gfskBw,
            (uint8_t)(fdevReg >> 16),
            (uint8_t)(fdevReg >> 8),
            (uint8_t)(fdevReg)
        };
        _writeCmd(CMD_SET_MODULATION_PARAMS, buf, 8);
        return;
    }

    // Enable LDRO when symbol time > 16 ms unless overridden
    uint8_t ldro = 0x00;
    if (_cfg.ldroMode == LDRO_ON) {
        ldro = 0x01;
    } else if (_cfg.ldroMode == LDRO_OFF) {
        ldro = 0x00;
    } else {
        float symbolMs = (float)(1UL << _cfg.spreadingFactor) / bwToKHz(_cfg.bandwidth);
        ldro = (symbolMs > 16.0f) ? 0x01 : 0x00;
    }

    uint8_t buf[4] = {
        _cfg.spreadingFactor,
        _cfg.bandwidth,
        _cfg.codingRate,
        ldro
    };
    _writeCmd(CMD_SET_MODULATION_PARAMS, buf, 4);

    _applyBwWorkaround();
}

void SX1262Radio::_applyPacketParams(uint8_t payloadLen) {
    if (_cfg.modem == MODEM_GFSK) {
        uint8_t lenField = payloadLen;
        if (_cfg.implicitHeader) lenField = _cfg.implicitPayloadLen;

        // GFSK packet params
        uint8_t buf[9] = {
            (uint8_t)(_cfg.gfskPreambleBits >> 8),
            (uint8_t)(_cfg.gfskPreambleBits & 0xFF),
            0x00,
            _cfg.gfskSyncLen,
            0x00,
            _cfg.implicitHeader ? (uint8_t)0x00 : (uint8_t)0x01,
            lenField,
            _cfg.crcOn ? (uint8_t)0x02 : (uint8_t)0x00,
            _cfg.gfskWhiteningOn ? (uint8_t)0x01 : (uint8_t)0x00
        };
        _writeCmd(CMD_SET_PACKET_PARAMS, buf, 9);
        return;
    }

    uint8_t headerType = _cfg.implicitHeader ? (uint8_t)0x01 : (uint8_t)0x00;
    uint8_t lenField = payloadLen;

    if (_cfg.implicitHeader) {
        lenField = _cfg.implicitPayloadLen;
    }

    uint8_t buf[6] = {
        (uint8_t)(_cfg.preambleLength >> 8),
        (uint8_t)(_cfg.preambleLength & 0xFF),
        headerType,
        lenField,
        _cfg.crcOn    ? (uint8_t)0x01 : (uint8_t)0x00,
        _cfg.iqInverted ? (uint8_t)0x01 : (uint8_t)0x00
    };
    _writeCmd(CMD_SET_PACKET_PARAMS, buf, 6);

    _applyIqWorkaround();
}

void SX1262Radio::_applyRxGain() {
    uint8_t rxGain = _cfg.rxBoosted ? (uint8_t)0x96 : (uint8_t)0x94;
    _writeReg(REG_RXGAIN, &rxGain, 1);
}

void SX1262Radio::_applyRegulatorMode() {
    uint8_t mode = _cfg.regulatorDcdc ? (uint8_t)0x01 : (uint8_t)0x00;
    _writeCmd(CMD_SET_REGULATOR_MODE, &mode, 1);
}

void SX1262Radio::_applySymbolTimeout() {
    uint8_t symbols = _cfg.symbolTimeout;
    _writeCmd(CMD_SET_LORA_SYMB_TIMEOUT, &symbols, 1);
}

void SX1262Radio::_applyFrequency() {
    uint32_t pll = (uint32_t)((double)_cfg.frequencyHz / 32000000.0 * 33554432.0);
    uint8_t buf[4] = {
        (uint8_t)(pll >> 24), (uint8_t)(pll >> 16),
        (uint8_t)(pll >> 8),  (uint8_t)(pll)
    };
    _writeCmd(CMD_SET_RF_FREQUENCY, buf, 4);
}

void SX1262Radio::_applyTxPower() {
    uint8_t buf[2] = { (uint8_t)_cfg.txPowerDbm, 0x07 };
    _writeCmd(CMD_SET_TX_PARAMS, buf, 2);
}

void SX1262Radio::_applySyncWord() {
    if (_cfg.modem != MODEM_LORA) return;

    uint8_t sw[2] = {
        (uint8_t)((_cfg.syncWord & 0xF0) | 0x04),
        (uint8_t)(((_cfg.syncWord & 0x0F) << 4) | 0x04)
    };
    _writeReg(REG_LR_SYNCWORD, sw, 2);
}

void SX1262Radio::_applyIqWorkaround() {
    uint8_t val = 0;
    _readReg(REG_IQ_POLARITY, &val, 1);
    if (_cfg.iqInverted) val &= ~(1 << 2);
    else                 val |=  (1 << 2);
    _writeReg(REG_IQ_POLARITY, &val, 1);
}

void SX1262Radio::_applyBwWorkaround() {
    uint8_t val = 0;
    _readReg(REG_TX_MODULATION, &val, 1);
    if (_cfg.bandwidth == BW_500K) val &= ~(1 << 2);
    else                           val |=  (1 << 2);
    _writeReg(REG_TX_MODULATION, &val, 1);
}

void SX1262Radio::_calibrate() {
    uint8_t calAll = 0x7F;
    _writeCmd(CMD_CALIBRATE, &calAll, 1);
    delay(10);
    _waitBusy();
}

void SX1262Radio::_calibrateImage(uint32_t freqHz) {
    uint8_t calFreq[2];
    if (freqHz >= 902000000UL) {
        calFreq[0] = 0xE1; calFreq[1] = 0xE9;
    } else if (freqHz >= 863000000UL) {
        calFreq[0] = 0xD7; calFreq[1] = 0xDB;
    } else if (freqHz >= 779000000UL) {
        calFreq[0] = 0xC1; calFreq[1] = 0xC5;
    } else if (freqHz >= 470000000UL) {
        calFreq[0] = 0x75; calFreq[1] = 0x81;
    } else {
        calFreq[0] = 0x6B; calFreq[1] = 0x6F;
    }
    _writeCmd(CMD_CALIBRATE_IMAGE, calFreq, 2);
    delay(5);
    _waitBusy();
}

void SX1262Radio::_reconfigure() {
    RadioState prev = _state;
    goStandby();
    uint8_t pktType = (_cfg.modem == MODEM_LORA) ? (uint8_t)0x01 : (uint8_t)0x00;
    _writeCmd(CMD_SET_PACKET_TYPE, &pktType, 1);
    _applyRegulatorMode();
    _applyModulation();
    _applyPacketParams();
    _applyTxPower();
    _applyFrequency();
    _applySyncWord();
    _applyRxGain();
    _applySymbolTimeout();
    if (prev == RADIO_RX) startReceive();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Public Methods – Configuration Setters
// ═════════════════════════════════════════════════════════════════════════════

void SX1262Radio::setFrequency(uint32_t hz)      { _cfg.frequencyHz = hz;  _reconfigure(); }
void SX1262Radio::setSpreadingFactor(uint8_t sf)  { if (sf>=5 && sf<=12) { _cfg.spreadingFactor=sf; _reconfigure(); } }
void SX1262Radio::setBandwidth(uint8_t bw)        { _cfg.bandwidth = bw;   _reconfigure(); }
void SX1262Radio::setCodingRate(uint8_t cr)       { if (cr>=5 && cr<=8) { _cfg.codingRate=cr-4; _reconfigure(); } }
void SX1262Radio::setTxPower(int8_t dbm)          { _cfg.txPowerDbm = constrain(dbm,-9,22); _reconfigure(); }
void SX1262Radio::setPreambleLength(uint16_t len) { _cfg.preambleLength = len; _reconfigure(); }
void SX1262Radio::setCrc(bool on)                 { _cfg.crcOn = on;       _reconfigure(); }
void SX1262Radio::setIqInverted(bool inv)         { _cfg.iqInverted = inv; _reconfigure(); }
void SX1262Radio::setSyncWord(uint8_t sw)         { _cfg.syncWord = sw;    _reconfigure(); }
void SX1262Radio::setModem(ModemType modem)       { _cfg.modem = modem;    _reconfigure(); }
void SX1262Radio::setGfskBitrate(uint32_t bps)    {
    if (bps < 600) bps = 600;
    if (bps > 300000) bps = 300000;
    _cfg.gfskBitrate = bps;
    _reconfigure();
}
void SX1262Radio::setGfskFdev(uint32_t hz)        {
    if (hz < 100) hz = 100;
    if (hz > 200000) hz = 200000;
    _cfg.gfskFdev = hz;
    _reconfigure();
}
void SX1262Radio::setGfskBw(uint8_t bwCode)       { _cfg.gfskBw = bwCode; _reconfigure(); }
void SX1262Radio::setGfskWhitening(bool on)       { _cfg.gfskWhiteningOn = on; _reconfigure(); }
void SX1262Radio::setHeaderImplicit(bool on, uint8_t payloadLen) {
    _cfg.implicitHeader = on;
    if (payloadLen == 0) payloadLen = 1;
    _cfg.implicitPayloadLen = payloadLen;
    _reconfigure();
}
void SX1262Radio::setLdroMode(LdroMode mode)      { _cfg.ldroMode = mode;  _reconfigure(); }
void SX1262Radio::setSymbolTimeout(uint8_t symbols) { _cfg.symbolTimeout = symbols; _reconfigure(); }
void SX1262Radio::setRxBoosted(bool on)           { _cfg.rxBoosted = on;   _reconfigure(); }
void SX1262Radio::setStandbyXosc(bool on)         { _cfg.standbyXosc = on; }
void SX1262Radio::setRegulatorDcdc(bool on)       { _cfg.regulatorDcdc = on; _reconfigure(); }

uint16_t SX1262Radio::readIrqStatus() {
    return _getIrqStatus();
}

void SX1262Radio::clearIrqStatus() {
    _clearIrq();
}

uint16_t SX1262Radio::readDeviceErrors() {
    uint8_t buf[2] = {};
    _readCmd(CMD_GET_DEVICE_ERRORS, buf, 2);
    return ((uint16_t)buf[0] << 8) | buf[1];
}

void SX1262Radio::clearDeviceErrors() {
    uint8_t clear[2] = { 0x00, 0x00 };
    _writeCmd(CMD_CLR_DEVICE_ERRORS, clear, 2);
}

uint8_t SX1262Radio::readRegister8(uint16_t addr) {
    uint8_t val = 0;
    _readReg(addr, &val, 1);
    return val;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Public Methods – Lifecycle & Radio Control
// ═════════════════════════════════════════════════════════════════════════════

bool SX1262Radio::begin(uint32_t freqHz) {
    Serial1.println("\n[INIT] SX1262 Driver starting...");

    // GPIO Setup
    pinMode(SX1262_NSS_PIN,   OUTPUT);   digitalWrite(SX1262_NSS_PIN, HIGH);
    pinMode(SX1262_RESET_PIN, OUTPUT);   digitalWrite(SX1262_RESET_PIN, HIGH);
    pinMode(SX1262_BUSY_PIN,  INPUT_PULLUP);
    pinMode(SX1262_TXEN_PIN,  OUTPUT);   _rfOff();
    pinMode(SX1262_RXEN_PIN,  OUTPUT);

    // SPI Init
    radioSPI.begin();
    radioSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    delay(10);

    // Hardware Reset
    _hardReset();

    uint32_t t0 = millis();
    while (digitalRead(SX1262_BUSY_PIN) == HIGH && millis() - t0 < 2000) delay(10);
    if (digitalRead(SX1262_BUSY_PIN) == HIGH) {
        Serial1.println("[ERR] Chip not responding after reset");
        return false;
    }
    Serial1.println("[INIT] Chip ready");
    Serial1.flush();

    // Wakeup
    Serial1.println("[INIT] Wakeup...");
    Serial1.flush();
    digitalWrite(SX1262_NSS_PIN, LOW);
    radioSPI.transfer(0xC0);
    radioSPI.transfer(0x00);
    digitalWrite(SX1262_NSS_PIN, HIGH);
    delay(10);
    Serial1.println("[INIT] Wakeup done");
    Serial1.flush();

    // Standby RC
    Serial1.println("[INIT] Standby RC...");
    Serial1.flush();
    uint8_t mode;
    mode = 0x00; _writeCmd(CMD_SET_STANDBY, &mode, 1);  delay(5);

    // DC-DC regulator
    Serial1.println("[INIT] DC-DC...");
    Serial1.flush();
    mode = 0x01; _writeCmd(CMD_SET_REGULATOR_MODE, &mode, 1);

    // Crystal trimming capacitors
    Serial1.println("[INIT] Crystal trim...");
    Serial1.flush();
    uint8_t trimCaps[2] = { 0x04, 0x2F };
    _writeReg(REG_XTATRIM, trimCaps, 2);

    // Full calibration
    Serial1.println("[INIT] Calibrate...");
    Serial1.flush();
    _calibrate();

    // Image rejection calibration
    Serial1.println("[INIT] Calibrate image...");
    Serial1.flush();
    _calibrateImage(freqHz);

    // Standby XOSC
    Serial1.println("[INIT] Standby XOSC...");
    Serial1.flush();
    mode = 0x01; _writeCmd(CMD_SET_STANDBY, &mode, 1);

    // FIFO base addresses
    Serial1.println("[INIT] FIFO...");
    Serial1.flush();
    uint8_t fifo[2] = { 0x00, 0x00 };
    _writeCmd(CMD_SET_BUFFER_BASE_ADDR, fifo, 2);

    // Packet type: LoRa
    Serial1.println("[INIT] Packet type LoRa...");
    Serial1.flush();
    mode = 0x01; _writeCmd(CMD_SET_PACKET_TYPE, &mode, 1);

    // PA Config
    Serial1.println("[INIT] PA config...");
    Serial1.flush();
    uint8_t pa[4] = { 0x04, 0x07, 0x00, 0x01 };
    _writeCmd(CMD_SET_PA_CONFIG, pa, 4);

    // TX clamp
    Serial1.println("[INIT] TX clamp...");
    Serial1.flush();
    uint8_t clamp = 0;
    _readReg(REG_TX_CLAMP_CFG, &clamp, 1);
    clamp |= 0x1E;
    _writeReg(REG_TX_CLAMP_CFG, &clamp, 1);

    // RX gain
    Serial1.println("[INIT] RX gain...");
    Serial1.flush();
    uint8_t rxGain = 0x96;
    _writeReg(REG_RXGAIN, &rxGain, 1);

    // DIO / IRQ configuration
    Serial1.println("[INIT] IRQ config...");
    Serial1.flush();
    uint16_t irqMask = IRQ_TX_DONE | IRQ_RX_DONE | IRQ_CRC_ERROR | IRQ_HEADER_ERROR | IRQ_TIMEOUT;
    uint8_t irq[8] = {
        (uint8_t)(irqMask >> 8), (uint8_t)(irqMask & 0xFF),
        (uint8_t)(irqMask >> 8), (uint8_t)(irqMask & 0xFF),
        0x00, 0x00,
        0x00, 0x00
    };
    _writeCmd(CMD_SET_DIO_IRQ_PARAMS, irq, 8);
    _clearIrq();

    // Default config
    _cfg.frequencyHz     = freqHz;
    _cfg.modem           = MODEM_LORA;
    _cfg.spreadingFactor = 9;       // SF9
    _cfg.bandwidth       = BW_125K; // 125 kHz
    _cfg.codingRate      = 2;       // CR 4/6
    _cfg.txPowerDbm      = 22;      // +22 dBm
    _cfg.preambleLength  = 8;       // 8 symbols
    _cfg.crcOn           = false;
    _cfg.iqInverted      = false;
    _cfg.syncWord        = 0x12;
    _cfg.implicitHeader  = false;
    _cfg.implicitPayloadLen = 0xFF;
    _cfg.ldroMode        = LDRO_AUTO;
    _cfg.symbolTimeout   = 0;
    _cfg.rxBoosted       = true;
    _cfg.standbyXosc     = true;
    _cfg.regulatorDcdc   = true;
    _cfg.gfskBitrate     = 50000;
    _cfg.gfskFdev        = 25000;
    _cfg.gfskPulseShape  = 0x09;
    _cfg.gfskBw          = 0x1E;
    _cfg.gfskPreambleBits = 32;
    _cfg.gfskSyncLen     = 3;
    _cfg.gfskWhiteningOn = true;

    _applyRegulatorMode();
    _applyModulation();
    _applyPacketParams();
    _applyTxPower();
    _applyFrequency();
    _applySyncWord();
    _applyRxGain();
    _applySymbolTimeout();

    _state = RADIO_IDLE;

    Serial1.print("[INIT] Frequency: ");
    Serial1.print((float)freqHz / 1e6f, 3);
    Serial1.println(" MHz");
    Serial1.println("[INIT] SF9  BW125  CR4/6  22dBm  CRC:OFF  Sync:0x12");
    Serial1.println("[INIT] SX1262 ready\n");
    return true;
}

void SX1262Radio::goStandby() {
    _rfOff();
    uint8_t mode = _cfg.standbyXosc ? (uint8_t)0x01 : (uint8_t)0x00;
    _writeCmd(CMD_SET_STANDBY, &mode, 1);
    _state = RADIO_IDLE;
}

bool SX1262Radio::send(const uint8_t* data, uint8_t len, uint32_t timeoutMs) {
    if (len == 0 || len > 255) return false;

    goStandby();
    _txSwitch();

    _applyPacketParams(len);

    _waitBusy();
    digitalWrite(SX1262_NSS_PIN, LOW);
    radioSPI.transfer(CMD_WRITE_BUFFER);
    radioSPI.transfer(0x00);
    for (uint8_t i = 0; i < len; i++) radioSPI.transfer(data[i]);
    digitalWrite(SX1262_NSS_PIN, HIGH);

    _clearIrq();

    uint32_t rtcSteps = (uint32_t)timeoutMs * 64;
    if (rtcSteps > 0x00FFFFFE) rtcSteps = 0x00FFFFFE;
    uint8_t txBuf[3] = {
        (uint8_t)((rtcSteps >> 16) & 0xFF),
        (uint8_t)((rtcSteps >> 8)  & 0xFF),
        (uint8_t)( rtcSteps        & 0xFF)
    };
    _writeCmd(CMD_SET_TX, txBuf, 3);
    _state = RADIO_TX;

    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        uint16_t irq = _getIrqStatus();
        if (irq & IRQ_TX_DONE)  { _clearIrq(); goStandby(); return true; }
        if (irq & IRQ_TIMEOUT)  { _clearIrq(); goStandby(); return false; }
        delay(1);
    }

    _clearIrq();
    goStandby();
    return false;
}

bool SX1262Radio::startReceive() {
    _rxSwitch();
    _applyPacketParams(0xFF);
    _clearIrq();

    uint8_t buf[3] = { 0xFF, 0xFF, 0xFF };
    _writeCmd(CMD_SET_RX, buf, 3);
    _state = RADIO_RX;
    return true;
}

bool SX1262Radio::checkForPacket(PacketInfo& pkt) {
    uint16_t irq = _getIrqStatus();

    if (!(irq & IRQ_RX_DONE)) return false;

    pkt.timestamp = millis();
    pkt.crcError  = (irq & IRQ_CRC_ERROR) != 0;

    // Read RX buffer status → [payloadLen, bufferOffset]
    uint8_t rxStat[2] = {};
    _readCmd(CMD_GET_RX_BUFFER_STATUS, rxStat, 2);
    pkt.length = rxStat[0];
    uint8_t offset = rxStat[1];

    // Read payload from buffer
    if (pkt.length > 0) {
        _waitBusy();
        digitalWrite(SX1262_NSS_PIN, LOW);
        radioSPI.transfer(CMD_READ_BUFFER);
        radioSPI.transfer(offset);
        radioSPI.transfer(0x00);
        for (uint8_t i = 0; i < pkt.length; i++)
            pkt.data[i] = radioSPI.transfer(0x00);
        digitalWrite(SX1262_NSS_PIN, HIGH);
    }

    // Read packet status → [rssiPkt, snrPkt, signalRssiPkt]
    uint8_t pktStat[3] = {};
    _readCmd(CMD_GET_PKT_STATUS, pktStat, 3);
    if (_cfg.modem == MODEM_LORA) {
        pkt.rssi       = -((int16_t)pktStat[0]) / 2;
        pkt.snr        =  (int8_t)pktStat[1];
        pkt.signalRssi = -((int16_t)pktStat[2]) / 2;
    } else {
        pkt.rssi       = -((int16_t)pktStat[0]) / 2;
        pkt.snr        = 0;
        pkt.signalRssi = pkt.rssi;
    }

    _pktCount++;
    if (pkt.crcError) _crcErrors++;

    _clearIrq();
    return true;
}

int16_t SX1262Radio::readRssi() {
    uint8_t buf[1] = {};
    _readCmd(CMD_GET_RSSI_INST, buf, 1);
    return -((int16_t)buf[0]) / 2;
}
