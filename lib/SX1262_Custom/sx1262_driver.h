#ifndef SX1262_DRIVER_H
#define SX1262_DRIVER_H

#include <Arduino.h>
#include <SPI.h>

// ═════════════════════════════════════════════════════════════════════════════
// Hardware Pin Definitions (DX-LR20 module + STM32F103C8)
// ═════════════════════════════════════════════════════════════════════════════
#define SX1262_NSS_PIN    PA4
#define SX1262_RESET_PIN  PA3
#define SX1262_BUSY_PIN   PA2
#define SX1262_TXEN_PIN   PA0
#define SX1262_RXEN_PIN   PA1

// ═════════════════════════════════════════════════════════════════════════════
// SX1262 Command OpCodes
// ═════════════════════════════════════════════════════════════════════════════
#define CMD_SET_SLEEP               0x84
#define CMD_SET_STANDBY             0x80
#define CMD_SET_FS                  0xC1
#define CMD_SET_TX                  0x83
#define CMD_SET_RX                  0x82
#define CMD_SET_PACKET_TYPE         0x8A
#define CMD_SET_RF_FREQUENCY        0x86
#define CMD_SET_TX_PARAMS           0x8E
#define CMD_SET_MODULATION_PARAMS   0x8B
#define CMD_SET_PACKET_PARAMS       0x8C
#define CMD_SET_BUFFER_BASE_ADDR    0x8F
#define CMD_WRITE_BUFFER            0x0E
#define CMD_READ_BUFFER             0x1E
#define CMD_GET_IRQ_STATUS          0x12
#define CMD_CLEAR_IRQ_STATUS        0x02
#define CMD_SET_DIO_IRQ_PARAMS      0x08
#define CMD_GET_STATUS              0xC0
#define CMD_SET_REGULATOR_MODE      0x96
#define CMD_CALIBRATE               0x89
#define CMD_SET_PA_CONFIG           0x95
#define CMD_GET_RX_BUFFER_STATUS    0x13
#define CMD_GET_PKT_STATUS          0x14
#define CMD_GET_RSSI_INST           0x15
#define CMD_WRITE_REGISTER          0x0D
#define CMD_READ_REGISTER           0x1D
#define CMD_GET_DEVICE_ERRORS       0x17
#define CMD_CLR_DEVICE_ERRORS       0x07
#define CMD_SET_LORA_SYMB_TIMEOUT    0xA0
#define CMD_CALIBRATE_IMAGE         0x98

// ═════════════════════════════════════════════════════════════════════════════
// SX1262 Register Addresses
// ═════════════════════════════════════════════════════════════════════════════
#define REG_LR_SYNCWORD         0x0740
#define REG_XTATRIM             0x0911
#define REG_IQ_POLARITY         0x0736
#define REG_TX_MODULATION       0x0889
#define REG_TX_CLAMP_CFG        0x08D8
#define REG_RXGAIN              0x08AC

// ═════════════════════════════════════════════════════════════════════════════
// IRQ Masks
// ═════════════════════════════════════════════════════════════════════════════
#define IRQ_TX_DONE             0x0001
#define IRQ_RX_DONE             0x0002
#define IRQ_PREAMBLE_DETECTED   0x0004
#define IRQ_HEADER_VALID        0x0010
#define IRQ_HEADER_ERROR        0x0020
#define IRQ_CRC_ERROR           0x0040
#define IRQ_TIMEOUT             0x0200
#define IRQ_ALL                 0xFFFF

// ═════════════════════════════════════════════════════════════════════════════
// LoRa Bandwidth Register Codes  (non-sequential mapping)
// ═════════════════════════════════════════════════════════════════════════════
#define BW_7K8     0x00
#define BW_10K4    0x08
#define BW_15K6    0x01
#define BW_20K8    0x09
#define BW_31K25   0x02
#define BW_41K7    0x0A
#define BW_62K5    0x03
#define BW_125K    0x04
#define BW_250K    0x05
#define BW_500K    0x06

// ═════════════════════════════════════════════════════════════════════════════
// Data Structures
// ═════════════════════════════════════════════════════════════════════════════

enum RadioState : uint8_t {
    RADIO_IDLE,
    RADIO_TX,
    RADIO_RX
};

enum LdroMode : uint8_t {
    LDRO_AUTO = 0,
    LDRO_ON,
    LDRO_OFF
};

enum ModemType : uint8_t {
    MODEM_GFSK = 0,
    MODEM_LORA = 1
};

struct LoRaConfig {
    ModemType modem;
    uint32_t frequencyHz;
    uint8_t  spreadingFactor;
    uint8_t  bandwidth;
    uint8_t  codingRate;
    int8_t   txPowerDbm;
    uint16_t preambleLength;
    bool     crcOn;
    bool     iqInverted;
    uint8_t  syncWord;
    bool     implicitHeader;
    uint8_t  implicitPayloadLen;
    LdroMode ldroMode;
    uint8_t  symbolTimeout;
    bool     rxBoosted;
    bool     standbyXosc;
    bool     regulatorDcdc;

    uint32_t gfskBitrate;
    uint32_t gfskFdev;
    uint8_t  gfskPulseShape;
    uint8_t  gfskBw;
    uint16_t gfskPreambleBits;
    uint8_t  gfskSyncLen;
    bool     gfskWhiteningOn;
};

struct PacketInfo {
    uint8_t  data[255];
    uint8_t  length;
    int16_t  rssi;
    int16_t  signalRssi;
    int8_t   snr;
    bool     crcError;
    uint32_t timestamp;
};

// ═════════════════════════════════════════════════════════════════════════════
// SX1262Radio Class
// ═════════════════════════════════════════════════════════════════════════════

class SX1262Radio {
public:
    // ── Lifecycle ────────────────────────────────────────────────────────────
    bool begin(uint32_t freqHz = 433000000);

    // ── Transmit / Receive ──────────────────────────────────────────────────
    bool send(const uint8_t* data, uint8_t len, uint32_t timeoutMs = 5000);
    bool startReceive();
    void goStandby();
    bool checkForPacket(PacketInfo& pkt);
    int16_t readRssi();

    // ── Configuration (apply immediately, safe to call in any state) ────────
    void setFrequency(uint32_t hz);
    void setSpreadingFactor(uint8_t sf);
    void setBandwidth(uint8_t bwCode);
    void setCodingRate(uint8_t cr);
    void setTxPower(int8_t dbm);
    void setPreambleLength(uint16_t len);
    void setCrc(bool on);
    void setIqInverted(bool inv);
    void setSyncWord(uint8_t sw);
    void setModem(ModemType modem);
    void setGfskBitrate(uint32_t bps);
    void setGfskFdev(uint32_t hz);
    void setGfskBw(uint8_t bwCode);
    void setGfskWhitening(bool on);
    void setHeaderImplicit(bool on, uint8_t payloadLen = 0xFF);
    void setLdroMode(LdroMode mode);
    void setSymbolTimeout(uint8_t symbols);
    void setRxBoosted(bool on);
    void setStandbyXosc(bool on);
    void setRegulatorDcdc(bool on);

    // ── Diagnostics ────────────────────────────────────────────────────────
    uint16_t readIrqStatus();
    void     clearIrqStatus();
    uint16_t readDeviceErrors();
    void     clearDeviceErrors();
    uint8_t  readRegister8(uint16_t addr);
    ModemType getModem() const { return _cfg.modem; }

    // ── Getters ─────────────────────────────────────────────────────────────
    const LoRaConfig& getConfig()   const { return _cfg; }
    RadioState        getState()    const { return _state; }
    uint32_t    getPacketCount()    const { return _pktCount; }
    uint32_t    getCrcErrorCount()  const { return _crcErrors; }
    void        resetStats()              { _pktCount = 0; _crcErrors = 0; }

    // ── Static Bandwidth Utilities ──────────────────────────────────────────
    static uint8_t bwFromKHz(float kHz);
    static const char* bwToStr(uint8_t bw);
    static float bwToKHz(uint8_t bw);

private:
    LoRaConfig  _cfg;
    RadioState  _state = RADIO_IDLE;
    uint32_t    _pktCount  = 0;
    uint32_t    _crcErrors = 0;

    // ── Internal Config Application ─────────────────────────────────────────
    void _applyModulation();
    void _applyPacketParams(uint8_t payloadLen = 0xFF);
    void _applyFrequency();
    void _applyTxPower();
    void _applySyncWord();
    void _applyIqWorkaround();
    void _applyBwWorkaround();
    void _applyRxGain();
    void _applyRegulatorMode();
    void _applySymbolTimeout();
    void _calibrate();
    void _calibrateImage(uint32_t freqHz);
    void _reconfigure();

    // ── Hardware Abstraction Layer ──────────────────────────────────────────
    void     _waitBusy();
    void     _hardReset();
    void     _writeCmd(uint8_t cmd, const uint8_t* d, uint8_t n);
    void     _readCmd(uint8_t cmd, uint8_t* d, uint8_t n);
    void     _writeReg(uint16_t addr, const uint8_t* d, uint8_t n);
    void     _readReg(uint16_t addr, uint8_t* d, uint8_t n);
    void     _txSwitch();
    void     _rxSwitch();
    void     _rfOff();
    void     _clearIrq();
    uint16_t _getIrqStatus();
};

#endif // SX1262_DRIVER_H
