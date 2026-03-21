/**
 * @file    sx1262_driver.h
 * @brief   Object-oriented SX1262 LoRa driver for DX-LR30 + STM32F103C8
 * @details Full-featured driver with dynamic configuration, sniff mode support,
 *          and detailed packet metadata. Based on verified manufacturer init sequence.
 */
#ifndef SX1262_DRIVER_H
#define SX1262_DRIVER_H

#include <Arduino.h>
#include <SPI.h>

// ═════════════════════════════════════════════════════════════════════════════
// Hardware Pin Definitions  (DX-LR30 module + STM32F103C8 "Blue Pill")
// ═════════════════════════════════════════════════════════════════════════════
#define SX1262_NSS_PIN    PA4   // SPI Chip Select
#define SX1262_RESET_PIN  PA3   // Hardware Reset (active LOW)
#define SX1262_BUSY_PIN   PA2   // Busy indicator (HIGH = processing)
#define SX1262_TXEN_PIN   PA0   // RF switch: TX enable
#define SX1262_RXEN_PIN   PA1   // RF switch: RX enable
// SPI1: PA5=SCK, PA6=MISO, PA7=MOSI  (handled by SPI.begin())
// DIO1:  PC15  (optional, not used in polling mode)

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
#define REG_LR_SYNCWORD         0x0740  // LoRa sync word (2 bytes)
#define REG_XTATRIM             0x0911  // Crystal trimming capacitor
#define REG_IQ_POLARITY         0x0736  // IQ polarity workaround
#define REG_TX_MODULATION       0x0889  // TX modulation (500kHz BW workaround)
#define REG_TX_CLAMP_CFG        0x08D8  // TX clamp (antenna mismatch workaround)
#define REG_RXGAIN              0x08AC  // RX gain (0x94=power-save, 0x96=boosted)

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
#define BW_7K8     0x00   //   7.81 kHz
#define BW_10K4    0x08   //  10.42 kHz
#define BW_15K6    0x01   //  15.63 kHz
#define BW_20K8    0x09   //  20.83 kHz
#define BW_31K25   0x02   //  31.25 kHz
#define BW_41K7    0x0A   //  41.67 kHz
#define BW_62K5    0x03   //  62.50 kHz
#define BW_125K    0x04   // 125.00 kHz
#define BW_250K    0x05   // 250.00 kHz
#define BW_500K    0x06   // 500.00 kHz

// ═════════════════════════════════════════════════════════════════════════════
// Data Structures
// ═════════════════════════════════════════════════════════════════════════════

/** @brief Current radio operating state */
enum RadioState : uint8_t {
    RADIO_IDLE,     // Standby, not transmitting or receiving
    RADIO_TX,       // Currently transmitting
    RADIO_RX        // Continuous receive active
};

/** @brief Low data-rate optimization mode for LoRa modulation params */
enum LdroMode : uint8_t {
    LDRO_AUTO = 0,
    LDRO_ON,
    LDRO_OFF
};

/** @brief Active packet modem type */
enum ModemType : uint8_t {
    MODEM_GFSK = 0,
    MODEM_LORA = 1
};

/** @brief Full radio configuration (all user-tuneable parameters) */
struct LoRaConfig {
    ModemType modem;           // Active modem (LoRa or GFSK)
    uint32_t frequencyHz;       // Operating frequency in Hz
    uint8_t  spreadingFactor;   // 5..12
    uint8_t  bandwidth;         // BW_xxx register code
    uint8_t  codingRate;        // 1..4  (meaning 4/5 .. 4/8)
    int8_t   txPowerDbm;        // -9 .. +22 dBm
    uint16_t preambleLength;    // Preamble symbols
    bool     crcOn;             // CRC enabled
    bool     iqInverted;        // Inverted IQ polarity
    uint8_t  syncWord;          // Logical sync word (0x12=private, 0x34=public)
    bool     implicitHeader;    // true=implicit, false=explicit
    uint8_t  implicitPayloadLen;// Valid when implicitHeader=true
    LdroMode ldroMode;          // Low data-rate optimization override
    uint8_t  symbolTimeout;     // LoRa symbol timeout (0 means disabled)
    bool     rxBoosted;         // RX gain boosted mode
    bool     standbyXosc;       // Standby mode selection (true=XOSC, false=RC)
    bool     regulatorDcdc;     // Regulator selection (true=DC-DC, false=LDO)

    // GFSK config fields
    uint32_t gfskBitrate;       // bits per second
    uint32_t gfskFdev;          // Hz
    uint8_t  gfskPulseShape;    // SX1262 pulse shape code
    uint8_t  gfskBw;            // SX1262 GFSK RX BW code
    uint16_t gfskPreambleBits;  // Preamble length in bits
    uint8_t  gfskSyncLen;       // Sync word length in bytes
    bool     gfskWhiteningOn;   // Whitening enable
};

/** @brief Metadata for a received packet */
struct PacketInfo {
    uint8_t  data[255];     // Raw received payload
    uint8_t  length;        // Payload length in bytes
    int16_t  rssi;          // Packet RSSI  (dBm)
    int16_t  signalRssi;    // Signal RSSI  (dBm)
    int8_t   snr;           // Raw SNR (divide by 4 for dB)
    bool     crcError;      // CRC mismatch detected
    uint32_t timestamp;     // millis() at reception
};

// ═════════════════════════════════════════════════════════════════════════════
// SX1262Radio Class
// ═════════════════════════════════════════════════════════════════════════════

class SX1262Radio {
public:
    // ── Lifecycle ────────────────────────────────────────────────────────────
    /** Initialise hardware and apply default configuration */
    bool begin(uint32_t freqHz = 433000000);

    // ── Transmit / Receive ──────────────────────────────────────────────────
    /** Blocking transmit.  Returns true on TX_DONE, false on timeout. */
    bool send(const uint8_t* data, uint8_t len, uint32_t timeoutMs = 5000);

    /** Enter continuous RX mode (non-blocking). */
    bool startReceive();

    /** Return radio to standby (idle). */
    void goStandby();

    /** Poll for a received packet.  Returns true if one was read. */
    bool checkForPacket(PacketInfo& pkt);

    /** Read instantaneous RSSI (works even with no packet). */
    int16_t readRssi();

    // ── Configuration (apply immediately, safe to call in any state) ────────
    void setFrequency(uint32_t hz);
    void setSpreadingFactor(uint8_t sf);
    void setBandwidth(uint8_t bwCode);
    void setCodingRate(uint8_t cr);         // 5..8 → stored as 1..4
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
    /** Find closest BW register code from a kHz value (e.g. 125.0 → BW_125K) */
    static uint8_t bwFromKHz(float kHz);
    /** Human-readable string for a BW code */
    static const char* bwToStr(uint8_t bw);
    /** Numeric kHz value for a BW code */
    static float bwToKHz(uint8_t bw);

private:
    LoRaConfig  _cfg;                   // Current configuration mirror
    RadioState  _state = RADIO_IDLE;    // Current operating state
    uint32_t    _pktCount  = 0;         // Total packets received
    uint32_t    _crcErrors = 0;         // Total CRC errors

    // ── Internal Config Application ─────────────────────────────────────────
    void _applyModulation();            // SF, BW, CR, LDRO → chip
    void _applyPacketParams(uint8_t payloadLen = 0xFF); // Preamble, CRC, IQ → chip
    void _applyFrequency();             // Frequency → chip
    void _applyTxPower();               // Power + ramp → chip
    void _applySyncWord();              // Sync word → register 0x0740
    void _applyIqWorkaround();          // IQ polarity register workaround
    void _applyBwWorkaround();          // 500 kHz TX modulation workaround
    void _applyRxGain();                // RX gain boost / power-save
    void _applyRegulatorMode();         // DC-DC or LDO
    void _applySymbolTimeout();         // LoRa symbol timeout command
    void _calibrate();                  // Full block calibration (RC, PLL, ADC)
    void _calibrateImage(uint32_t freqHz); // Frequency-band image rejection cal
    void _reconfigure();                // Standby → apply all → resume old state

    // ── Hardware Abstraction Layer ──────────────────────────────────────────
    void     _waitBusy();                                          // Block until BUSY LOW
    void     _hardReset();                                         // Pulse RESET
    void     _writeCmd(uint8_t cmd, const uint8_t* d, uint8_t n); // SPI command write
    void     _readCmd(uint8_t cmd, uint8_t* d, uint8_t n);        // SPI command read
    void     _writeReg(uint16_t addr, const uint8_t* d, uint8_t n);
    void     _readReg(uint16_t addr, uint8_t* d, uint8_t n);
    void     _txSwitch();               // RF switch → TX path
    void     _rxSwitch();               // RF switch → RX path
    void     _rfOff();                  // Both switches off
    void     _clearIrq();               // Clear all IRQ flags
    uint16_t _getIrqStatus();           // Read 16-bit IRQ register
};

#endif // SX1262_DRIVER_H
