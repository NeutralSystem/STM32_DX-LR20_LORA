/**
 * @file    main.cpp
 * @brief   Serial CLI for STM32 + SX1262 LoRa radio (Meshtastic-style terminal)
 * @details Provides a rich serial command interface for sending/receiving LoRa
 *          messages, sniffing packets with detailed metadata, and changing all
 *          radio parameters on the fly.
 *
 *  Serial port: Serial1 (UART1 PA9=TX, PA10=RX) at 115200 baud.
 */
#include <Arduino.h>
#include "sx1262_driver.h"

// ═════════════════════════════════════════════════════════════════════════════
//  Application State
// ═════════════════════════════════════════════════════════════════════════════

/** High-level operating mode of the application */
enum AppMode : uint8_t {
    MODE_IDLE,          // Standby – accepts commands
    MODE_SNIFFING,      // Continuous RX with verbose packet display
    MODE_BEACONING,     // Periodic TX of a fixed message
    MODE_ANALYZE,       // Live RSSI analyzer graph
    MODE_CHAT           // Encrypted chat room mode
};

// Global radio instance
static SX1262Radio radio;

// CLI line buffer
static char    cmdBuf[256];
static uint16_t cmdLen = 0;

// Application mode tracking
static AppMode appMode = MODE_IDLE;

// Beacon state
static char     beaconMsg[256];
static uint8_t  beaconLen      = 0;
static uint32_t beaconInterval = 0;
static uint32_t lastBeaconTime = 0;

// Sniff-mode periodic RSSI reporting
static uint32_t lastSniffStatus = 0;
static bool     meshDecodeHint = false;

// Analyzer-mode state
static uint32_t analyzeStartHz   = 0;
static uint32_t analyzeEndHz     = 0;
static uint32_t analyzeStepHz    = 0;
static uint32_t analyzePrevFreq  = 433000000;
static uint32_t lastAnalyzeFrame = 0;
static bool     analyzeNeedsClear = false;
static bool     analyzePeakHold = true;
static int16_t  analyzeThresholdDbm = -95;
static int16_t  analyzeHoldPeakRssi = -200;
static uint32_t analyzeHoldPeakFreq = 0;
static const uint16_t ANALYZE_MAX_BINS = 96;

// Chat mode state
static bool     chatJoined = false;
static uint8_t  chatRoomId = 0;
static char     chatNick[13] = "node";
static uint8_t  chatKeyEnc[16] = {0};
static uint8_t  chatKeyMac[16] = {0};
static uint16_t chatSenderId = 1;
static uint32_t chatSeq = 0;
static uint32_t lastChatStatus = 0;

static const uint8_t CHAT_MAGIC0 = 0xC7;
static const uint8_t CHAT_MAGIC1 = 0xA7;
static const uint8_t CHAT_MAX_MSG = 120;
static const uint8_t CHAT_MAX_PEERS = 8;

struct ChatPeerState {
    uint16_t senderId;
    uint32_t lastSeq;
    bool used;
};

static ChatPeerState chatPeers[CHAT_MAX_PEERS];

// LED pin
#define LED_PIN PC13

// ═════════════════════════════════════════════════════════════════════════════
//  Utility Helpers
// ═════════════════════════════════════════════════════════════════════════════

/** Print a hex dump of data with ASCII sidebar (16 bytes per row) */
static void hexDump(const uint8_t* data, uint8_t len) {
    char tmp[8];
    for (uint16_t i = 0; i < len; i += 16) {
        // Address
        snprintf(tmp, sizeof(tmp), "  %04X:", i);
        Serial1.print(tmp);

        // Hex bytes with mid-row separator
        for (uint8_t j = 0; j < 16; j++) {
            if (j == 8) Serial1.print(' ');
            if (i + j < len) {
                snprintf(tmp, sizeof(tmp), " %02X", data[i + j]);
                Serial1.print(tmp);
            } else {
                Serial1.print("   ");
            }
        }

        // ASCII column
        Serial1.print("  ");
        for (uint8_t j = 0; j < 16 && (i + j) < len; j++) {
            char c = (char)data[i + j];
            Serial1.print((c >= 32 && c <= 126) ? c : '.');
        }
        Serial1.println();
    }
}

static bool protoReadVarint(const uint8_t* data, uint8_t len, uint8_t& pos, uint64_t& out) {
    out = 0;
    uint8_t shift = 0;
    while (pos < len && shift < 64) {
        uint8_t b = data[pos++];
        out |= ((uint64_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

static bool protoSkipField(const uint8_t* data, uint8_t len, uint8_t& pos, uint8_t wireType) {
    uint64_t v = 0;
    switch (wireType) {
        case 0:
            return protoReadVarint(data, len, pos, v);
        case 1:
            if (pos + 8 > len) return false;
            pos += 8;
            return true;
        case 2:
            if (!protoReadVarint(data, len, pos, v)) return false;
            if (v > (uint64_t)(len - pos)) return false;
            pos += (uint8_t)v;
            return true;
        case 5:
            if (pos + 4 > len) return false;
            pos += 4;
            return true;
        default:
            return false;
    }
}

static bool displayMeshtasticLite(const uint8_t* data, uint8_t len) {
    uint8_t pos = 0;

    bool hasFrom = false, hasTo = false, hasId = false, hasChannel = false, hasHop = false;
    uint32_t fromNode = 0, toNode = 0, pktId = 0, channel = 0, hopLimit = 0;
    bool hasPortNum = false;
    uint32_t portNum = 0;
    bool hasPlainPayloadLen = false;
    uint32_t plainPayloadLen = 0;
    bool hasEncryptedLen = false;
    uint32_t encryptedLen = 0;

    while (pos < len) {
        uint64_t key = 0;
        if (!protoReadVarint(data, len, pos, key)) return false;
        uint32_t field = (uint32_t)(key >> 3);
        uint8_t wire = (uint8_t)(key & 0x07);

        if (wire == 0) {
            uint64_t v = 0;
            if (!protoReadVarint(data, len, pos, v)) return false;
            if (field == 1) { hasFrom = true; fromNode = (uint32_t)v; }
            else if (field == 2) { hasTo = true; toNode = (uint32_t)v; }
            else if (field == 3) { hasId = true; pktId = (uint32_t)v; }
            else if (field == 7) { hasChannel = true; channel = (uint32_t)v; }
            else if (field == 12) { hasHop = true; hopLimit = (uint32_t)v; }
            continue;
        }

        if (wire == 2) {
            uint64_t l64 = 0;
            if (!protoReadVarint(data, len, pos, l64)) return false;
            if (l64 > (uint64_t)(len - pos)) return false;
            uint8_t l = (uint8_t)l64;
            const uint8_t* p = data + pos;

            if (field == 10) {
                hasEncryptedLen = true;
                encryptedLen = l;
            } else if (field == 11) {
                uint8_t sp = 0;
                while (sp < l) {
                    uint64_t skey = 0;
                    if (!protoReadVarint(p, l, sp, skey)) break;
                    uint32_t sf = (uint32_t)(skey >> 3);
                    uint8_t sw = (uint8_t)(skey & 0x07);

                    if (sw == 0) {
                        uint64_t sv = 0;
                        if (!protoReadVarint(p, l, sp, sv)) break;
                        if (sf == 1) {
                            hasPortNum = true;
                            portNum = (uint32_t)sv;
                        }
                    } else if (sw == 2) {
                        uint64_t sl64 = 0;
                        if (!protoReadVarint(p, l, sp, sl64)) break;
                        if (sl64 > (uint64_t)(l - sp)) break;
                        if (sf == 2) {
                            hasPlainPayloadLen = true;
                            plainPayloadLen = (uint32_t)sl64;
                        }
                        sp += (uint8_t)sl64;
                    } else if (!protoSkipField(p, l, sp, sw)) {
                        break;
                    }
                }
            }

            pos += l;
            continue;
        }

        if (!protoSkipField(data, len, pos, wire)) return false;
    }

    bool looksMesh = (hasFrom || hasTo || hasId) && (hasEncryptedLen || hasPortNum || hasChannel);
    if (!looksMesh) return false;

    Serial1.println("  --- Meshtastic Lite ---");
    if (hasFrom) { Serial1.print("  From:       0x"); Serial1.println(fromNode, HEX); }
    if (hasTo)   { Serial1.print("  To:         0x"); Serial1.println(toNode, HEX); }
    if (hasId)   { Serial1.print("  Packet ID:  0x"); Serial1.println(pktId, HEX); }
    if (hasChannel) { Serial1.print("  Channel:    "); Serial1.println(channel); }
    if (hasHop)  { Serial1.print("  Hop limit:  "); Serial1.println(hopLimit); }
    if (hasPortNum) { Serial1.print("  PortNum:    "); Serial1.println(portNum); }
    if (hasPlainPayloadLen) { Serial1.print("  Decoded payload bytes: "); Serial1.println(plainPayloadLen); }
    if (hasEncryptedLen) { Serial1.print("  Encrypted bytes: "); Serial1.println(encryptedLen); }
    return true;
}

/** Convert a single hex character to its numeric value (0-15), or 0xFF on error */
static uint8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0xFF;
}

/** Print the prompt character */
static void prompt() {
    if (appMode == MODE_IDLE) Serial1.print("\n> ");
}

static uint16_t deviceId16() {
    volatile const uint32_t* uid0 = (uint32_t*)0x1FFFF7E8;
    volatile const uint32_t* uid1 = (uint32_t*)0x1FFFF7EC;
    volatile const uint32_t* uid2 = (uint32_t*)0x1FFFF7F0;
    uint32_t mix = (*uid0) ^ (*uid1) ^ (*uid2);
    uint16_t id = (uint16_t)(mix ^ (mix >> 16));
    return id ? id : 1;
}

static uint64_t rotl64(uint64_t x, uint8_t b) {
    return (x << b) | (x >> (64 - b));
}

static uint64_t readU64LE(const uint8_t* p) {
    uint64_t v = 0;
    for (uint8_t i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}

static void writeU64LE(uint8_t* p, uint64_t v) {
    for (uint8_t i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void xteaEncryptBlock(uint32_t v[2], const uint32_t k[4]) {
    uint32_t v0 = v[0], v1 = v[1], sum = 0, delta = 0x9E3779B9;
    for (uint8_t i = 0; i < 32; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
        sum += delta;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

static void xteaCtrCrypt(uint8_t* data, uint8_t len, const uint8_t key[16], uint64_t nonceBase) {
    uint32_t k[4];
    for (uint8_t i = 0; i < 4; i++) {
        k[i] = ((uint32_t)key[i * 4 + 0] << 24) |
               ((uint32_t)key[i * 4 + 1] << 16) |
               ((uint32_t)key[i * 4 + 2] << 8) |
               ((uint32_t)key[i * 4 + 3]);
    }

    uint8_t offset = 0;
    uint64_t ctr = 0;
    while (offset < len) {
        uint8_t ks[8];
        uint64_t block = nonceBase ^ ctr;
        writeU64LE(ks, block);
        uint32_t v[2] = {
            ((uint32_t)ks[0] << 24) | ((uint32_t)ks[1] << 16) | ((uint32_t)ks[2] << 8) | ks[3],
            ((uint32_t)ks[4] << 24) | ((uint32_t)ks[5] << 16) | ((uint32_t)ks[6] << 8) | ks[7]
        };
        xteaEncryptBlock(v, k);
        ks[0] = (uint8_t)(v[0] >> 24); ks[1] = (uint8_t)(v[0] >> 16);
        ks[2] = (uint8_t)(v[0] >> 8);  ks[3] = (uint8_t)(v[0]);
        ks[4] = (uint8_t)(v[1] >> 24); ks[5] = (uint8_t)(v[1] >> 16);
        ks[6] = (uint8_t)(v[1] >> 8);  ks[7] = (uint8_t)(v[1]);

        uint8_t n = (uint8_t)min((int)(len - offset), 8);
        for (uint8_t i = 0; i < n; i++) data[offset + i] ^= ks[i];
        offset += n;
        ctr++;
    }
}

static uint64_t sipHash24(const uint8_t* data, uint16_t len, const uint8_t key[16]) {
    uint64_t k0 = readU64LE(key);
    uint64_t k1 = readU64LE(key + 8);
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;

    auto sipRound = [&]() {
        v0 += v1; v1 = rotl64(v1, 13); v1 ^= v0; v0 = rotl64(v0, 32);
        v2 += v3; v3 = rotl64(v3, 16); v3 ^= v2;
        v0 += v3; v3 = rotl64(v3, 21); v3 ^= v0;
        v2 += v1; v1 = rotl64(v1, 17); v1 ^= v2; v2 = rotl64(v2, 32);
    };

    const uint8_t* p = data;
    uint16_t left = len;
    while (left >= 8) {
        uint64_t m = readU64LE(p);
        v3 ^= m;
        sipRound(); sipRound();
        v0 ^= m;
        p += 8;
        left -= 8;
    }

    uint64_t b = ((uint64_t)len) << 56;
    for (uint8_t i = 0; i < left; i++) b |= ((uint64_t)p[i]) << (8 * i);
    v3 ^= b;
    sipRound(); sipRound();
    v0 ^= b;
    v2 ^= 0xff;
    sipRound(); sipRound(); sipRound(); sipRound();
    return v0 ^ v1 ^ v2 ^ v3;
}

static bool parseHex16(const char* s, uint8_t out[16]) {
    if (strlen(s) != 32) return false;
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t hi = hexNibble(s[i * 2]);
        uint8_t lo = hexNibble(s[i * 2 + 1]);
        if (hi > 15 || lo > 15) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool parseHexFlexible(const char* s, uint8_t out[16]) {
    size_t n = strlen(s);
    if (n < 8 || n > 32 || (n & 1) != 0) return false;

    uint8_t tmp[16] = {0};
    uint8_t bytes = (uint8_t)(n / 2);
    for (uint8_t i = 0; i < bytes; i++) {
        uint8_t hi = hexNibble(s[i * 2]);
        uint8_t lo = hexNibble(s[i * 2 + 1]);
        if (hi > 15 || lo > 15) return false;
        tmp[i] = (uint8_t)((hi << 4) | lo);
    }

    // Stretch/compress provided key bytes to 16 bytes deterministically.
    for (uint8_t i = 0; i < 16; i++) {
        out[i] = tmp[i % bytes] ^ (uint8_t)(0xA5 + i * 11);
    }
    return true;
}

static uint32_t prngState = 0;

static uint32_t xorshift32() {
    uint32_t x = prngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    prngState = x ? x : 0x6C8E9CF5UL;
    return prngState;
}

static void generateChatKey(uint8_t out[16]) {
    if (prngState == 0) {
        prngState = ((uint32_t)deviceId16() << 16) ^ millis() ^ micros() ^ 0xA341316CUL;
    }
    for (uint8_t i = 0; i < 16; i += 4) {
        uint32_t r = xorshift32();
        out[i + 0] = (uint8_t)(r >> 24);
        out[i + 1] = (uint8_t)(r >> 16);
        out[i + 2] = (uint8_t)(r >> 8);
        out[i + 3] = (uint8_t)r;
    }
}

static void printHex16(const uint8_t key[16]) {
    for (uint8_t i = 0; i < 16; i++) {
        if (key[i] < 0x10) Serial1.print('0');
        Serial1.print(key[i], HEX);
    }
    Serial1.println();
}

static void deriveMacKey(const uint8_t enc[16], uint8_t mac[16]) {
    static const uint8_t salt[16] = {
        0x3A,0x7C,0x11,0xE2,0x94,0x57,0x26,0xC1,
        0x5D,0xB0,0x48,0xF3,0x9E,0x62,0x17,0xAD
    };
    for (uint8_t i = 0; i < 16; i++) mac[i] = enc[i] ^ salt[i];
}

static int8_t findPeerSlot(uint16_t senderId) {
    for (uint8_t i = 0; i < CHAT_MAX_PEERS; i++) {
        if (chatPeers[i].used && chatPeers[i].senderId == senderId) return (int8_t)i;
    }
    for (uint8_t i = 0; i < CHAT_MAX_PEERS; i++) {
        if (!chatPeers[i].used) return (int8_t)i;
    }
    return -1;
}

static bool acceptSeq(uint16_t senderId, uint32_t seq) {
    int8_t slot = findPeerSlot(senderId);
    if (slot < 0) return false;
    if (!chatPeers[slot].used) {
        chatPeers[slot].used = true;
        chatPeers[slot].senderId = senderId;
        chatPeers[slot].lastSeq = seq;
        return true;
    }
    if (seq <= chatPeers[slot].lastSeq) return false;
    chatPeers[slot].lastSeq = seq;
    return true;
}

static uint8_t buildChatPacket(const char* msg, uint8_t* out) {
    uint8_t msgLen = (uint8_t)strlen(msg);
    if (msgLen == 0 || msgLen > CHAT_MAX_MSG) return 0;

    uint8_t nickLen = (uint8_t)strlen(chatNick);
    if (nickLen > 12) nickLen = 12;

    uint8_t plain[160];
    uint8_t plainLen = 0;
    plain[plainLen++] = nickLen;
    memcpy(&plain[plainLen], chatNick, nickLen);
    plainLen += nickLen;
    plain[plainLen++] = msgLen;
    memcpy(&plain[plainLen], msg, msgLen);
    plainLen += msgLen;

    out[0] = CHAT_MAGIC0;
    out[1] = CHAT_MAGIC1;
    out[2] = chatRoomId;
    out[3] = (uint8_t)(chatSenderId >> 8);
    out[4] = (uint8_t)(chatSenderId & 0xFF);
    out[5] = (uint8_t)(chatSeq >> 24);
    out[6] = (uint8_t)(chatSeq >> 16);
    out[7] = (uint8_t)(chatSeq >> 8);
    out[8] = (uint8_t)(chatSeq);
    out[9] = plainLen;
    memcpy(&out[10], plain, plainLen);

    uint64_t nonce = ((uint64_t)chatRoomId << 56) | ((uint64_t)chatSenderId << 40) | (uint64_t)chatSeq;
    xteaCtrCrypt(&out[10], plainLen, chatKeyEnc, nonce);

    uint64_t tag = sipHash24(&out[2], (uint16_t)(8 + plainLen), chatKeyMac);
    writeU64LE(&out[10 + plainLen], tag);

    return (uint8_t)(10 + plainLen + 8);
}

static bool parseChatPacket(const uint8_t* in, uint8_t len, char* nickOut, uint8_t nickOutLen, char* msgOut, uint8_t msgOutLen) {
    if (len < 18) return false;
    if (in[0] != CHAT_MAGIC0 || in[1] != CHAT_MAGIC1) return false;
    if (!chatJoined) return false;
    if (in[2] != chatRoomId) return false;

    uint16_t senderId = ((uint16_t)in[3] << 8) | in[4];
    uint32_t seq = ((uint32_t)in[5] << 24) | ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 8) | in[8];
    uint8_t cLen = in[9];
    if ((uint16_t)(10 + cLen + 8) != len) return false;
    if (senderId == chatSenderId) return false;

    uint64_t tagCalc = sipHash24(&in[2], (uint16_t)(8 + cLen), chatKeyMac);
    uint64_t tagRx = readU64LE(&in[10 + cLen]);
    if (tagCalc != tagRx) return false;

    if (!acceptSeq(senderId, seq)) return false;

    uint8_t plain[160];
    memcpy(plain, &in[10], cLen);
    uint64_t nonce = ((uint64_t)in[2] << 56) | ((uint64_t)senderId << 40) | (uint64_t)seq;
    xteaCtrCrypt(plain, cLen, chatKeyEnc, nonce);

    uint8_t idx = 0;
    if (idx >= cLen) return false;
    uint8_t nickLen = plain[idx++];
    if (nickLen > 12 || (uint16_t)(idx + nickLen + 1) > cLen) return false;

    uint8_t copyNick = (uint8_t)min((int)nickLen, (int)(nickOutLen - 1));
    memcpy(nickOut, &plain[idx], copyNick);
    nickOut[copyNick] = '\0';
    idx += nickLen;

    uint8_t msgLen = plain[idx++];
    if ((uint16_t)(idx + msgLen) > cLen) return false;
    uint8_t copyMsg = (uint8_t)min((int)msgLen, (int)(msgOutLen - 1));
    memcpy(msgOut, &plain[idx], copyMsg);
    msgOut[copyMsg] = '\0';
    return true;
}

/** Apply built-in analyzer presets by ISM band label */
static bool applyAnalyzePreset(const char* preset, uint32_t& startHz, uint32_t& endHz, uint32_t& stepHz) {
    if (strcasecmp(preset, "433") == 0) {
        startHz = 430000000UL; endHz = 440000000UL; stepHz = 50000UL; return true;
    }
    if (strcasecmp(preset, "868") == 0) {
        startHz = 863000000UL; endHz = 870000000UL; stepHz = 50000UL; return true;
    }
    if (strcasecmp(preset, "915") == 0) {
        startHz = 902000000UL; endHz = 928000000UL; stepHz = 200000UL; return true;
    }
    return false;
}

/** Parse analyzer args. Default: +/-1 MHz around current frequency, 25 kHz step. */
static bool parseAnalyzeArgs(const char* arg, uint32_t& startHz, uint32_t& endHz, uint32_t& stepHz) {
    if (*arg == '\0') {
        uint32_t center = radio.getConfig().frequencyHz;
        startHz = (center > 1000000UL) ? (center - 1000000UL) : 150000000UL;
        endHz = center + 1000000UL;
        if (startHz < 150000000UL) startHz = 150000000UL;
        if (endHz > 960000000UL) endHz = 960000000UL;
        stepHz = 25000UL;
        return true;
    }

    if (applyAnalyzePreset(arg, startHz, endHz, stepHz)) return true;

    char* p1 = nullptr;
    float startMHz = strtof(arg, &p1);
    while (*p1 == ' ') p1++;

    char* p2 = nullptr;
    float endMHz = strtof(p1, &p2);
    while (*p2 == ' ') p2++;

    float stepKHz = 25.0f;
    if (*p2 != '\0') stepKHz = strtof(p2, nullptr);

    if (startMHz < 150.0f || endMHz > 960.0f || startMHz >= endMHz || stepKHz < 1.0f) {
        return false;
    }

    startHz = (uint32_t)(startMHz * 1e6f);
    endHz = (uint32_t)(endMHz * 1e6f);
    stepHz = (uint32_t)(stepKHz * 1e3f);
    return (stepHz > 0);
}

/** Render one live analyzer frame to terminal using ANSI cursor control. */
static void renderAnalyzeFrame() {
    const uint16_t maxBins = ANALYZE_MAX_BINS;

    if (analyzeEndHz <= analyzeStartHz || analyzeStepHz == 0) return;

    uint32_t spanHz = analyzeEndHz - analyzeStartHz;
    uint32_t requestedBins = (spanHz / analyzeStepHz) + 1;
    uint32_t effectiveStepHz = analyzeStepHz;
    uint16_t bins = (requestedBins > maxBins) ? maxBins : (uint16_t)requestedBins;

    if (requestedBins > maxBins) {
        effectiveStepHz = spanHz / (maxBins - 1);
        if (effectiveStepHz == 0) effectiveStepHz = 1;
    }

    static int16_t rssiBins[ANALYZE_MAX_BINS];
    static uint32_t freqBins[ANALYZE_MAX_BINS];
    int16_t peakRssi = -200;
    uint32_t peakFreq = analyzeStartHz;
    uint16_t hits = 0;

    for (uint16_t i = 0; i < bins; i++) {
        uint32_t freq = analyzeStartHz + (uint32_t)i * effectiveStepHz;
        if (freq > analyzeEndHz) freq = analyzeEndHz;

        radio.setFrequency(freq);
        radio.startReceive();
        delayMicroseconds(800);

        int16_t rssi = radio.readRssi();
        radio.goStandby();

        rssiBins[i] = rssi;
        freqBins[i] = freq;
        if (rssi > peakRssi) {
            peakRssi = rssi;
            peakFreq = freq;
        }
        if (rssi >= analyzeThresholdDbm) hits++;
    }

    if (analyzePeakHold) {
        if (peakRssi > analyzeHoldPeakRssi) {
            analyzeHoldPeakRssi = peakRssi;
            analyzeHoldPeakFreq = peakFreq;
        }
    } else {
        analyzeHoldPeakRssi = peakRssi;
        analyzeHoldPeakFreq = peakFreq;
    }

    if (analyzeNeedsClear) {
        Serial1.print("\033[2J\033[H");
        analyzeNeedsClear = false;
    } else {
        Serial1.print("\033[H");
    }

    Serial1.print("analyze ");
    Serial1.print(analyzeStartHz / 1e6f, 3);
    Serial1.print("-");
    Serial1.print(analyzeEndHz / 1e6f, 3);
    Serial1.print("MHz step ");
    Serial1.print(effectiveStepHz / 1e3f, 1);
    Serial1.print("k thr ");
    Serial1.print(analyzeThresholdDbm);
    Serial1.print(" p ");
    Serial1.print(peakFreq / 1e6f, 3);
    Serial1.print("@");
    Serial1.print(peakRssi);
    Serial1.print(" h ");
    Serial1.print(analyzeHoldPeakFreq / 1e6f, 3);
    Serial1.print("@");
    Serial1.print(analyzeHoldPeakRssi);
    Serial1.print(" hits ");
    Serial1.println(hits);

    for (uint16_t i = 0; i < bins; i++) {
        Serial1.print(i);
        Serial1.print(": ");
        Serial1.print(freqBins[i] / 1e6f, 3);
        Serial1.print(" ");
        if (rssiBins[i] > -100) Serial1.print(' ');
        if (rssiBins[i] > -10) Serial1.print(' ');
        Serial1.print(rssiBins[i]);
        Serial1.print(" ");
        if (rssiBins[i] >= analyzeThresholdDbm) Serial1.print("*");
        if (freqBins[i] == peakFreq) Serial1.print("P");
        if (freqBins[i] == analyzeHoldPeakFreq) Serial1.print("H");
        Serial1.println();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Sniff-Mode Packet Display
// ═════════════════════════════════════════════════════════════════════════════

/** Pretty-print a received packet with full metadata */
static void displayPacket(const PacketInfo& pkt) {
    float snrDb = (float)pkt.snr / 4.0f;

    Serial1.println();
    Serial1.print("=== RX #");
    Serial1.print(radio.getPacketCount());
    Serial1.print(" === ");
    Serial1.print(pkt.timestamp / 1000.0f, 3);
    Serial1.println("s ================================");

    if (pkt.crcError) Serial1.println("  *** CRC ERROR ***");

    Serial1.print("  RSSI:       "); Serial1.print(pkt.rssi);       Serial1.println(" dBm");
    Serial1.print("  SNR:        "); Serial1.print(snrDb, 1);       Serial1.println(" dB");
    Serial1.print("  Signal RSSI:"); Serial1.print(pkt.signalRssi); Serial1.println(" dBm");
    Serial1.print("  Length:     "); Serial1.print(pkt.length);      Serial1.println(" bytes");

    if (pkt.length > 0) {
        Serial1.println("  --- Hex Dump ---");
        hexDump(pkt.data, pkt.length);

        // If the data looks like printable ASCII, show it as text
        bool printable = true;
        for (uint8_t i = 0; i < pkt.length && printable; i++)
            if (pkt.data[i] < 0x20 && pkt.data[i] != '\n' && pkt.data[i] != '\r')
                printable = false;

        if (printable) {
            Serial1.print("  ASCII:      ");
            Serial1.write(pkt.data, pkt.length);
            Serial1.println();
        }

        if (meshDecodeHint) {
            displayMeshtasticLite(pkt.data, pkt.length);
        }
    }

    Serial1.println("====================================================");
}

// ═════════════════════════════════════════════════════════════════════════════
//  Command Handlers
// ═════════════════════════════════════════════════════════════════════════════

/** Print all available commands */
static void cmdHelp() {
    Serial1.println();
    Serial1.println("================ LoRa Tool Help ================");
    Serial1.println("TX/RX");
    Serial1.println("  send <msg>            Send text payload");
    Serial1.println("  sendhex <hex>         Send raw payload bytes");
    Serial1.println("  sniff                 Continuous RX packet monitor");
    Serial1.println("  beacon <ms> <msg>     Periodic TX sender");
    Serial1.println("  stop                  Stop sniff/beacon/analyze/chat");

    Serial1.println("Encrypted Chat");
    Serial1.println("  chatjoin <room> [key] Join room (key optional: 8..32 hex)");
    Serial1.println("  chatnick <name>       Nickname (max 12 chars)");
    Serial1.println("  chat <message>        Send encrypted message (optional in room)");
    Serial1.println("  chatstatus            Show room/nick/sender ID");
    Serial1.println("  chatleave             Leave room and clear keys");
    Serial1.println("  (In chat room, plain text sends directly)");

    Serial1.println("Radio Config");
    Serial1.println("  modem <lora|gfsk>     Select active modem");
    Serial1.println("  freq <MHz>            RF center frequency");
    Serial1.println("  sf bw cr              LoRa PHY settings");
    Serial1.println("  power preamble crc iq syncword");
    Serial1.println("  header ldro symtimeout");
    Serial1.println("  bitrate fdev fskbw whitening   (GFSK)");
    Serial1.println("  rxboost standby regulator      HW behavior");

    Serial1.println("Scan / Analyze");
    Serial1.println("  scan [start end step] | 433 | 868 | 915 | all");
    Serial1.println("  scanpreset <band>     Preset scan helper");
    Serial1.println("  analyze [start end step]|433|868|915");
    Serial1.println("  analyzecfg peak on|off");
    Serial1.println("  analyzecfg threshold -95");
    Serial1.println("  meshlisten <preset> <MHz>       Meshtastic-like sniff preset");

    Serial1.println("Diagnostics/System");
    Serial1.println("  status rssi reset uptime version sleep clear reboot");
    Serial1.println("Tips: <cmd> -help, Ctrl+C/q stop modes, Ctrl+L clear");
}

/** Send a text message */
static void cmdSend(const char* arg) {
    uint8_t len = strlen(arg);
    if (len == 0) { Serial1.println("[ERR] Usage: send <message>"); return; }
    if (len > 255) { Serial1.println("[ERR] Message too long (max 255)"); return; }

    // Pause sniff mode temporarily if active
    bool resume = (appMode == MODE_SNIFFING);

    Serial1.print("[TX] Sending "); Serial1.print(len); Serial1.println(" bytes...");
    digitalWrite(LED_PIN, LOW);

    uint32_t t0 = millis();
    bool ok = radio.send((const uint8_t*)arg, len);
    uint32_t elapsed = millis() - t0;

    digitalWrite(LED_PIN, HIGH);

    if (ok) {
        Serial1.print("[TX] Complete in "); Serial1.print(elapsed); Serial1.println(" ms");
    } else {
        Serial1.println("[TX] FAILED (timeout)");
    }

    // Resume sniff if it was active
    if (resume) {
        radio.startReceive();
        Serial1.println("[RX] Sniff resumed");
    }
}

/** Send raw hex bytes */
static void cmdSendHex(const char* arg) {
    uint8_t buf[255];
    uint8_t len = 0;

    while (*arg && len < 255) {
        // Skip whitespace
        while (*arg == ' ') arg++;
        if (!*arg) break;

        uint8_t hi = hexNibble(*arg++);
        if (hi > 15 || !*arg) { Serial1.println("[ERR] Invalid hex"); return; }
        uint8_t lo = hexNibble(*arg++);
        if (lo > 15) { Serial1.println("[ERR] Invalid hex"); return; }

        buf[len++] = (hi << 4) | lo;
    }

    if (len == 0) { Serial1.println("[ERR] Usage: sendhex <hex bytes>"); return; }

    bool resume = (appMode == MODE_SNIFFING);

    Serial1.print("[TX] Sending "); Serial1.print(len); Serial1.println(" hex bytes...");
    digitalWrite(LED_PIN, LOW);

    uint32_t t0 = millis();
    bool ok = radio.send(buf, len);
    uint32_t elapsed = millis() - t0;

    digitalWrite(LED_PIN, HIGH);

    if (ok) {
        Serial1.print("[TX] Complete in "); Serial1.print(elapsed); Serial1.println(" ms");
    } else {
        Serial1.println("[TX] FAILED (timeout)");
    }

    if (resume) {
        radio.startReceive();
        Serial1.println("[RX] Sniff resumed");
    }
}

/** Enter sniff (continuous receive) mode */
static void cmdSniff() {
    if (appMode == MODE_BEACONING) {
        appMode = MODE_IDLE;
        radio.goStandby();
    }
    appMode = MODE_SNIFFING;
    radio.resetStats();
    radio.startReceive();
    lastSniffStatus = millis();

    const LoRaConfig& c = radio.getConfig();
    Serial1.print("[SNIFF] Listening on ");
    Serial1.print((float)c.frequencyHz / 1e6f, 3);
    Serial1.print(" MHz  SF");
    Serial1.print(c.spreadingFactor);
    Serial1.print("  BW ");
    Serial1.print(SX1262Radio::bwToStr(c.bandwidth));
    Serial1.println("  (type 'stop' to exit)");
}

/** Start / stop beacon mode */
static void cmdBeacon(const char* arg) {
    // Parse interval
    char* end = nullptr;
    uint32_t interval = strtoul(arg, &end, 10);

    // Stop beacon if interval is 0
    if (interval == 0) {
        if (appMode == MODE_BEACONING) {
            appMode = MODE_IDLE;
            radio.goStandby();
            Serial1.println("[BEACON] Stopped");
        } else {
            Serial1.println("[ERR] Beacon not active");
        }
        return;
    }

    // Skip whitespace after interval number
    while (*end == ' ') end++;
    if (*end == '\0') { Serial1.println("[ERR] Usage: beacon <ms> <message>"); return; }

    // Stop any active mode
    if (appMode == MODE_SNIFFING) radio.goStandby();

    // Store beacon config
    beaconInterval = interval;
    beaconLen = strlen(end);
    if (beaconLen > 255) beaconLen = 255;
    memcpy(beaconMsg, end, beaconLen);
    beaconMsg[beaconLen] = '\0';

    appMode = MODE_BEACONING;
    lastBeaconTime = 0;  // Send immediately on first loop

    Serial1.print("[BEACON] Sending \"");
    Serial1.print(beaconMsg);
    Serial1.print("\" every ");
    Serial1.print(beaconInterval);
    Serial1.println(" ms");
}

/** Stop any active mode and go idle */
static void cmdStop() {
    radio.goStandby();

    if (appMode == MODE_SNIFFING) {
        Serial1.print("[SNIFF] Stopped.  Packets: ");
        Serial1.print(radio.getPacketCount());
        Serial1.print("  CRC errors: ");
        Serial1.println(radio.getCrcErrorCount());
    } else if (appMode == MODE_BEACONING) {
        Serial1.println("[BEACON] Stopped");
    } else if (appMode == MODE_ANALYZE) {
        radio.setFrequency(analyzePrevFreq);
        Serial1.println("[ANALYZE] Stopped");
    } else if (appMode == MODE_CHAT) {
        Serial1.println("[CHAT] Stopped listening");
    } else {
        Serial1.println("[INFO] Already idle");
    }
    appMode = MODE_IDLE;
}

/** Set frequency from a MHz string (supports decimals like 433.125) */
static void cmdFreq(const char* arg) {
    float mhz = atof(arg);
    if (mhz < 150.0f || mhz > 960.0f) {
        Serial1.println("[ERR] Frequency out of range (150-960 MHz)");
        return;
    }
    uint32_t hz = (uint32_t)(mhz * 1e6f);
    radio.setFrequency(hz);
    Serial1.print("[CFG] Frequency: ");
    Serial1.print(mhz, 3);
    Serial1.println(" MHz");
}

/** Set spreading factor */
static void cmdSF(const char* arg) {
    int sf = atoi(arg);
    if (sf < 5 || sf > 12) {
        Serial1.println("[ERR] SF must be 5-12");
        return;
    }
    radio.setSpreadingFactor((uint8_t)sf);
    Serial1.print("[CFG] Spreading Factor: SF"); Serial1.println(sf);
}

/** Set bandwidth from kHz value */
static void cmdBW(const char* arg) {
    float kHz = atof(arg);
    uint8_t code = SX1262Radio::bwFromKHz(kHz);
    radio.setBandwidth(code);
    Serial1.print("[CFG] Bandwidth: ");
    Serial1.println(SX1262Radio::bwToStr(code));
}

/** Set coding rate (user gives denominator 5-8 → 4/5 .. 4/8) */
static void cmdCR(const char* arg) {
    int cr = atoi(arg);
    if (cr < 5 || cr > 8) {
        Serial1.println("[ERR] CR must be 5-8 (meaning 4/5 to 4/8)");
        return;
    }
    radio.setCodingRate((uint8_t)cr);
    Serial1.print("[CFG] Coding Rate: 4/"); Serial1.println(cr);
}

/** Set TX power in dBm */
static void cmdPower(const char* arg) {
    int p = atoi(arg);
    if (p < -9 || p > 22) {
        Serial1.println("[ERR] Power must be -9 to 22 dBm");
        return;
    }
    radio.setTxPower((int8_t)p);
    Serial1.print("[CFG] TX Power: "); Serial1.print(p); Serial1.println(" dBm");
}

/** Set preamble length */
static void cmdPreamble(const char* arg) {
    int n = atoi(arg);
    if (n < 1 || n > 65535) {
        Serial1.println("[ERR] Preamble must be 1-65535");
        return;
    }
    radio.setPreambleLength((uint16_t)n);
    Serial1.print("[CFG] Preamble: "); Serial1.print(n); Serial1.println(" symbols");
}

/** Enable / disable CRC */
static void cmdCrc(const char* arg) {
    if (strcasecmp(arg, "on") == 0) {
        radio.setCrc(true);
        Serial1.println("[CFG] CRC: ON");
    } else if (strcasecmp(arg, "off") == 0) {
        radio.setCrc(false);
        Serial1.println("[CFG] CRC: OFF");
    } else {
        Serial1.println("[ERR] Usage: crc <on|off>");
    }
}

/** Set IQ polarity */
static void cmdIQ(const char* arg) {
    if (strcasecmp(arg, "normal") == 0) {
        radio.setIqInverted(false);
        Serial1.println("[CFG] IQ: Normal");
    } else if (strcasecmp(arg, "invert") == 0 || strcasecmp(arg, "inverted") == 0) {
        radio.setIqInverted(true);
        Serial1.println("[CFG] IQ: Inverted");
    } else {
        Serial1.println("[ERR] Usage: iq <normal|invert>");
    }
}

/** Set LoRa sync word (hex byte) */
static void cmdSyncWord(const char* arg) {
    uint8_t sw = (uint8_t)strtoul(arg, nullptr, 16);
    if (sw == 0 && arg[0] != '0') {
        Serial1.println("[ERR] Usage: syncword <hex> (e.g. 12 or 34)");
        return;
    }
    radio.setSyncWord(sw);
    Serial1.print("[CFG] Sync Word: 0x");
    if (sw < 0x10) Serial1.print('0');
    Serial1.print(sw, HEX);
    if (sw == 0x12)      Serial1.println(" (Private)");
    else if (sw == 0x34) Serial1.println(" (Public / LoRaWAN)");
    else                 Serial1.println();
}

/** Configure Meshtastic-like PHY presets and start passive sniffing. */
static void cmdMeshListen(const char* arg) {
    char preset[16] = {};
    const char* p = arg;

    while (*p == ' ') p++;
    if (*p == '\0') {
        Serial1.println("[ERR] Usage: meshlisten <longfast|longslow|shortfast> <freqMHz>");
        return;
    }

    uint8_t presetLen = 0;
    while (*p != '\0' && *p != ' ') {
        if (presetLen < sizeof(preset) - 1) {
            preset[presetLen++] = *p;
        }
        p++;
    }
    preset[presetLen] = '\0';

    while (*p == ' ') p++;
    if (*p == '\0') {
        Serial1.println("[ERR] Usage: meshlisten <longfast|longslow|shortfast> <freqMHz>");
        return;
    }

    char* end = nullptr;
    float mhz = strtof(p, &end);
    if (end == p) {
        Serial1.println("[ERR] Usage: meshlisten <longfast|longslow|shortfast> <freqMHz>");
        return;
    }
    while (*end == ' ') end++;
    if (*end != '\0') {
        Serial1.println("[ERR] Usage: meshlisten <longfast|longslow|shortfast> <freqMHz>");
        return;
    }

    if (mhz < 150.0f || mhz > 960.0f) {
        Serial1.println("[ERR] Frequency out of range (150-960 MHz)");
        return;
    }

    radio.setModem(MODEM_LORA);
    radio.setFrequency((uint32_t)(mhz * 1e6f));
    radio.setHeaderImplicit(false);
    radio.setCrc(true);
    radio.setIqInverted(false);
    radio.setPreambleLength(16);
    radio.setSyncWord(0x2B);

    if (strcasecmp(preset, "longfast") == 0) {
        radio.setSpreadingFactor(11);
        radio.setBandwidth(SX1262Radio::bwFromKHz(250.0f));
        radio.setCodingRate(5);
    } else if (strcasecmp(preset, "longslow") == 0) {
        radio.setSpreadingFactor(12);
        radio.setBandwidth(SX1262Radio::bwFromKHz(125.0f));
        radio.setCodingRate(8);
    } else if (strcasecmp(preset, "shortfast") == 0) {
        radio.setSpreadingFactor(7);
        radio.setBandwidth(SX1262Radio::bwFromKHz(250.0f));
        radio.setCodingRate(5);
    } else {
        Serial1.println("[ERR] Unknown preset. Use longfast, longslow, or shortfast.");
        return;
    }

    Serial1.print("[MESH] Preset: ");
    Serial1.print(preset);
    Serial1.print("  Freq: ");
    Serial1.print(mhz, 3);
    Serial1.println(" MHz");
    meshDecodeHint = true;
    Serial1.println("[MESH] Meshtastic-lite decode enabled (best effort).");

    cmdSniff();
}

/** Set header mode (explicit/implicit) and optional implicit length */
static void cmdHeader(const char* arg) {
    if (strncasecmp(arg, "explicit", 8) == 0) {
        radio.setHeaderImplicit(false);
        Serial1.println("[CFG] Header: Explicit");
        return;
    }

    if (strncasecmp(arg, "implicit", 8) == 0) {
        uint8_t len = 0xFF;
        const char* p = arg + 8;
        while (*p == ' ') p++;
        if (*p != '\0') {
            int n = atoi(p);
            if (n < 1 || n > 255) {
                Serial1.println("[ERR] Implicit length must be 1-255");
                return;
            }
            len = (uint8_t)n;
        }
        radio.setHeaderImplicit(true, len);
        Serial1.print("[CFG] Header: Implicit  Len=");
        Serial1.println(len);
        return;
    }

    Serial1.println("[ERR] Usage: header <explicit|implicit [len]>");
}

/** Set low data-rate optimization mode */
static void cmdLdro(const char* arg) {
    if (strcasecmp(arg, "auto") == 0) {
        radio.setLdroMode(LDRO_AUTO);
        Serial1.println("[CFG] LDRO: AUTO");
    } else if (strcasecmp(arg, "on") == 0) {
        radio.setLdroMode(LDRO_ON);
        Serial1.println("[CFG] LDRO: ON");
    } else if (strcasecmp(arg, "off") == 0) {
        radio.setLdroMode(LDRO_OFF);
        Serial1.println("[CFG] LDRO: OFF");
    } else {
        Serial1.println("[ERR] Usage: ldro <auto|on|off>");
    }
}

/** Set LoRa symbol timeout (0-255 symbols, 0 disables) */
static void cmdSymTimeout(const char* arg) {
    int n = atoi(arg);
    if (n < 0 || n > 255) {
        Serial1.println("[ERR] Usage: symtimeout <0-255>");
        return;
    }
    radio.setSymbolTimeout((uint8_t)n);
    Serial1.print("[CFG] Symbol timeout: ");
    Serial1.println(n);
}

/** Set RX gain mode */
static void cmdRxBoost(const char* arg) {
    if (strcasecmp(arg, "on") == 0) {
        radio.setRxBoosted(true);
        Serial1.println("[CFG] RX boost: ON");
    } else if (strcasecmp(arg, "off") == 0) {
        radio.setRxBoosted(false);
        Serial1.println("[CFG] RX boost: OFF");
    } else {
        Serial1.println("[ERR] Usage: rxboost <on|off>");
    }
}

/** Set standby source clock */
static void cmdStandby(const char* arg) {
    if (strcasecmp(arg, "xosc") == 0) {
        radio.setStandbyXosc(true);
        Serial1.println("[CFG] Standby: XOSC");
    } else if (strcasecmp(arg, "rc") == 0) {
        radio.setStandbyXosc(false);
        Serial1.println("[CFG] Standby: RC");
    } else {
        Serial1.println("[ERR] Usage: standby <xosc|rc>");
    }
}

/** Set regulator mode */
static void cmdRegulator(const char* arg) {
    if (strcasecmp(arg, "dcdc") == 0) {
        radio.setRegulatorDcdc(true);
        Serial1.println("[CFG] Regulator: DC-DC");
    } else if (strcasecmp(arg, "ldo") == 0) {
        radio.setRegulatorDcdc(false);
        Serial1.println("[CFG] Regulator: LDO");
    } else {
        Serial1.println("[ERR] Usage: regulator <dcdc|ldo>");
    }
}

/** Select active modem (lora/gfsk) */
static void cmdModem(const char* arg) {
    if (strcasecmp(arg, "lora") == 0) {
        radio.setModem(MODEM_LORA);
        Serial1.println("[CFG] Modem: LoRa");
    } else if (strcasecmp(arg, "gfsk") == 0 || strcasecmp(arg, "fsk") == 0) {
        radio.setModem(MODEM_GFSK);
        Serial1.println("[CFG] Modem: GFSK");
    } else {
        Serial1.println("[ERR] Usage: modem <lora|gfsk>");
    }
}

/** Set GFSK bitrate in bps */
static void cmdBitrate(const char* arg) {
    uint32_t bps = (uint32_t)strtoul(arg, nullptr, 10);
    if (bps < 600 || bps > 300000) {
        Serial1.println("[ERR] Usage: bitrate <600..300000>");
        return;
    }
    radio.setGfskBitrate(bps);
    Serial1.print("[CFG] GFSK bitrate: ");
    Serial1.println(bps);
}

/** Set GFSK frequency deviation in Hz */
static void cmdFdev(const char* arg) {
    uint32_t hz = (uint32_t)strtoul(arg, nullptr, 10);
    if (hz < 100 || hz > 200000) {
        Serial1.println("[ERR] Usage: fdev <100..200000>");
        return;
    }
    radio.setGfskFdev(hz);
    Serial1.print("[CFG] GFSK fdev: ");
    Serial1.println(hz);
}

/** Set GFSK RX bandwidth code (datasheet code byte) */
static void cmdFskBw(const char* arg) {
    uint8_t code = (uint8_t)strtoul(arg, nullptr, 16);
    radio.setGfskBw(code);
    Serial1.print("[CFG] GFSK BW code: 0x");
    if (code < 0x10) Serial1.print('0');
    Serial1.println(code, HEX);
}

/** Set GFSK whitening */
static void cmdWhitening(const char* arg) {
    if (strcasecmp(arg, "on") == 0) {
        radio.setGfskWhitening(true);
        Serial1.println("[CFG] GFSK whitening: ON");
    } else if (strcasecmp(arg, "off") == 0) {
        radio.setGfskWhitening(false);
        Serial1.println("[CFG] GFSK whitening: OFF");
    } else {
        Serial1.println("[ERR] Usage: whitening <on|off>");
    }
}

static void cmdChatStatus() {
    Serial1.print("[CHAT] Joined: ");
    Serial1.println(chatJoined ? "YES" : "NO");
    Serial1.print("[CHAT] Room: ");
    Serial1.println(chatRoomId);
    Serial1.print("[CHAT] Nick: ");
    Serial1.println(chatNick);
    Serial1.print("[CHAT] Sender ID: 0x");
    if (chatSenderId < 0x1000) Serial1.print('0');
    if (chatSenderId < 0x0100) Serial1.print('0');
    if (chatSenderId < 0x0010) Serial1.print('0');
    Serial1.println(chatSenderId, HEX);
}

static void cmdChatNick(const char* arg) {
    if (*arg == '\0') {
        Serial1.println("[ERR] Usage: chatnick <name>");
        return;
    }
    size_t n = strlen(arg);
    if (n > 12) n = 12;
    memcpy(chatNick, arg, n);
    chatNick[n] = '\0';
    Serial1.print("[CHAT] Nick set: ");
    Serial1.println(chatNick);
}

static void cmdChatJoin(const char* arg) {
    char roomStr[8] = {};
    char keyStr[80] = {};
    int n = sscanf(arg, "%7s %79s", roomStr, keyStr);
    if (n < 1) {
        Serial1.println("[ERR] Usage: chatjoin <room 0-255> [hexkey]");
        return;
    }

    int room = atoi(roomStr);
    if (room < 0 || room > 255) {
        Serial1.println("[ERR] Room must be 0-255");
        return;
    }

    if (n >= 2) {
        if (!parseHexFlexible(keyStr, chatKeyEnc)) {
            Serial1.println("[ERR] Key must be 8..32 hex chars (even length)");
            return;
        }
    } else {
        generateChatKey(chatKeyEnc);
        Serial1.println("[CHAT] Generated room key:");
        printHex16(chatKeyEnc);
        Serial1.println("[CHAT] Share this key with peers to join the same room.");
    }

    deriveMacKey(chatKeyEnc, chatKeyMac);
    chatRoomId = (uint8_t)room;
    chatJoined = true;
    chatSeq = 0;
    memset(chatPeers, 0, sizeof(chatPeers));

    appMode = MODE_CHAT;
    radio.startReceive();
    Serial1.print("[CHAT] Joined room ");
    Serial1.println(chatRoomId);
    Serial1.println("[CHAT] Type message text directly. Use 'chatleave' to exit.");
}

static void cmdChatLeave() {
    if (!chatJoined) {
        Serial1.println("[CHAT] Not joined");
        return;
    }
    chatJoined = false;
    memset(chatKeyEnc, 0, sizeof(chatKeyEnc));
    memset(chatKeyMac, 0, sizeof(chatKeyMac));
    memset(chatPeers, 0, sizeof(chatPeers));
    if (appMode == MODE_CHAT) {
        appMode = MODE_IDLE;
        radio.goStandby();
    }
    Serial1.println("[CHAT] Left room");
}

static void cmdChatSend(const char* arg) {
    if (!chatJoined) {
        Serial1.println("[ERR] Join a room first: chatjoin <room> [hexkey]");
        return;
    }
    if (*arg == '\0') {
        Serial1.println("[ERR] Usage: chat <message>");
        return;
    }

    uint8_t pkt[220];
    uint8_t len = buildChatPacket(arg, pkt);
    if (len == 0) {
        Serial1.println("[ERR] Chat message too long");
        return;
    }

    bool wasChat = (appMode == MODE_CHAT);
    bool ok = radio.send(pkt, len);
    if (ok) {
        chatSeq++;
        Serial1.print(chatNick);
        Serial1.print(" :> \"");
        Serial1.print(arg);
        Serial1.println("\"");
    } else {
        Serial1.println("[CHAT] TX FAIL");
    }

    if (wasChat) radio.startReceive();
}

/** Display full radio configuration summary */
static void cmdStatus() {
    const LoRaConfig& c = radio.getConfig();
    Serial1.println();
    Serial1.println("+------------ Radio Configuration ------------+");

    Serial1.print("| Modem:       ");
    Serial1.println(c.modem == MODEM_LORA ? "LoRa" : "GFSK");

    Serial1.print("| Frequency:   ");
    Serial1.print((float)c.frequencyHz / 1e6f, 3);
    Serial1.println(" MHz");

    Serial1.print("| Spreading:   SF");
    Serial1.println(c.spreadingFactor);

    Serial1.print("| Bandwidth:   ");
    Serial1.println(SX1262Radio::bwToStr(c.bandwidth));

    Serial1.print("| Coding Rate: 4/");
    Serial1.println(c.codingRate + 4);

    Serial1.print("| TX Power:    ");
    Serial1.print(c.txPowerDbm);
    Serial1.println(" dBm");

    Serial1.print("| Preamble:    ");
    Serial1.print(c.preambleLength);
    Serial1.println(" symbols");

    Serial1.print("| CRC:         ");
    Serial1.println(c.crcOn ? "ON" : "OFF");

    Serial1.print("| IQ:          ");
    Serial1.println(c.iqInverted ? "Inverted" : "Normal");

    Serial1.print("| Sync Word:   0x");
    if (c.syncWord < 0x10) Serial1.print('0');
    Serial1.print(c.syncWord, HEX);
    if (c.syncWord == 0x12)      Serial1.println(" (Private)");
    else if (c.syncWord == 0x34) Serial1.println(" (Public)");
    else                         Serial1.println();

    Serial1.print("| Header:      ");
    if (c.implicitHeader) {
        Serial1.print("Implicit (len=");
        Serial1.print(c.implicitPayloadLen);
        Serial1.println(")");
    } else {
        Serial1.println("Explicit");
    }

    Serial1.print("| LDRO:        ");
    if (c.ldroMode == LDRO_AUTO) Serial1.println("AUTO");
    else if (c.ldroMode == LDRO_ON) Serial1.println("ON");
    else Serial1.println("OFF");

    Serial1.print("| SymTimeout:  ");
    Serial1.println(c.symbolTimeout);

    Serial1.print("| RX Gain:     ");
    Serial1.println(c.rxBoosted ? "Boosted" : "Power-save");

    Serial1.print("| Standby:     ");
    Serial1.println(c.standbyXosc ? "XOSC" : "RC");

    Serial1.print("| Regulator:   ");
    Serial1.println(c.regulatorDcdc ? "DC-DC" : "LDO");

    if (c.modem == MODEM_GFSK) {
        Serial1.print("| GFSK BR:     ");
        Serial1.print(c.gfskBitrate);
        Serial1.println(" bps");

        Serial1.print("| GFSK Fdev:   ");
        Serial1.print(c.gfskFdev);
        Serial1.println(" Hz");

        Serial1.print("| GFSK BW:     0x");
        if (c.gfskBw < 0x10) Serial1.print('0');
        Serial1.println(c.gfskBw, HEX);

        Serial1.print("| Whitening:   ");
        Serial1.println(c.gfskWhiteningOn ? "ON" : "OFF");
    }

    // Current state
    Serial1.print("| State:       ");
    switch (appMode) {
        case MODE_IDLE:      Serial1.println("IDLE");      break;
        case MODE_SNIFFING:  Serial1.println("SNIFFING");  break;
        case MODE_BEACONING: Serial1.println("BEACONING"); break;
        case MODE_ANALYZE:   Serial1.println("ANALYZE");   break;
        case MODE_CHAT:      Serial1.println("CHAT");      break;
    }

    // Stats
    Serial1.print("| Packets RX:  ");
    Serial1.println(radio.getPacketCount());
    Serial1.print("| CRC Errors:  ");
    Serial1.println(radio.getCrcErrorCount());

    Serial1.println("+----------------------------------------------+");
}

/** Read and display current RSSI */
static void cmdRssi() {
    int16_t rssi = radio.readRssi();
    Serial1.print("[RSSI] "); Serial1.print(rssi); Serial1.println(" dBm");
}

/** Perform full radio reset and re-initialisation */
static void cmdReset() {
    appMode = MODE_IDLE;
    uint32_t freq = radio.getConfig().frequencyHz;
    Serial1.println("[RESET] Re-initialising radio...");
    radio.begin(freq);
}

// ═════════════════════════════════════════════════════════════════════════════
//  New System Commands
// ═════════════════════════════════════════════════════════════════════════════

/** Clear terminal screen using ANSI escape codes */
static void cmdClear() {
    Serial1.print("\033[2J\033[H");
}

/** Show time since last reset, formatted as days/hours/minutes/seconds */
static void cmdUptime() {
    uint32_t ms  = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hrs = min / 60;
    uint32_t days = hrs / 24;
    Serial1.print("[UPTIME] ");
    if (days > 0) { Serial1.print(days); Serial1.print("d "); }
    Serial1.print(hrs % 24);  Serial1.print("h ");
    Serial1.print(min % 60);  Serial1.print("m ");
    Serial1.print(sec % 60);  Serial1.println("s");
}

/** Show firmware version, build date, and hardware details */
static void cmdVersion() {
    Serial1.println();
    Serial1.println("+-------------- System Info -----------------+");
    Serial1.println("| Firmware:    STM32 LoRa Terminal v1.0");
    Serial1.print(  "| Built:       "); Serial1.print(__DATE__); Serial1.print(" "); Serial1.println(__TIME__);
    Serial1.println("| MCU:         STM32F103C8T6");
    Serial1.print(  "| Clock:       "); Serial1.print(SystemCoreClock / 1000000); Serial1.println(" MHz");
    Serial1.println("| Radio:       SX1262 (DX-LR30)");
    Serial1.println("| SPI:         SPI1 (PA5/PA6/PA7)");
    Serial1.println("| Serial:      UART1 (PA9/PA10) 115200");
    Serial1.println("+----------------------------------------------+");
}

/** Put radio into low-power standby and stop active modes */
static void cmdSleep() {
    if (appMode != MODE_IDLE) {
        radio.goStandby();
        appMode = MODE_IDLE;
    }
    radio.goStandby();
    Serial1.println("[SLEEP] Radio in standby (low power).");
    Serial1.println("        Use 'reset' to re-init or any TX/RX command to resume.");
}

/** Software reset the entire MCU */
static void cmdReboot() {
    Serial1.println("[REBOOT] Resetting MCU...");
    Serial1.flush();
    delay(100);
    NVIC_SystemReset();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Frequency Scanner
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Scan a frequency range, reading RSSI at each step.
 * On channels with signal (> -100 dBm), briefly listens for packets.
 * Returns false if the user aborted with Ctrl+C or 'q'.
 */
static bool scanFreqRange(float startMHz, float endMHz, float stepKHz,
                          const char* label) {
    uint32_t startHz = (uint32_t)(startMHz * 1e6f);
    uint32_t endHz   = (uint32_t)(endMHz   * 1e6f);
    uint32_t stepHz  = (uint32_t)(stepKHz  * 1e3f);

    int16_t  peakRssi    = -200;
    uint32_t peakFreq    = 0;
    uint16_t activeCount = 0;
    uint16_t totalSteps  = 0;

    // Band header
    Serial1.println();
    Serial1.print("--- "); Serial1.print(label);
    Serial1.print("  ");   Serial1.print(startMHz, 1);
    Serial1.print(" - ");  Serial1.print(endMHz, 1);
    Serial1.print(" MHz  step "); Serial1.print(stepKHz, 0);
    Serial1.println(" kHz ---");
    Serial1.println("  Freq (MHz)   RSSI      Level");
    Serial1.println("  ----------  ------  --------------------");

    for (uint32_t freq = startHz; freq <= endHz; freq += stepHz) {
        // Allow user abort: Ctrl+C or 'q'
        if (Serial1.available()) {
            char c = Serial1.peek();
            if (c == 0x03 || c == 'q' || c == 'Q') {
                Serial1.read();
                Serial1.println("\n  [Aborted by user]");
                return false;
            }
        }

        totalSteps++;

        // Tune to frequency, enter RX, wait for PLL lock + RSSI settle
        radio.setFrequency(freq);
        radio.startReceive();
        delayMicroseconds(800);

        int16_t rssi = radio.readRssi();

        // Track the strongest signal found
        if (rssi > peakRssi) { peakRssi = rssi; peakFreq = freq; }

        bool active = (rssi > -100);
        if (active) activeCount++;

        // Visual bar: map -130..-30 dBm to 0..20 '#' characters
        int bar = (rssi + 130) / 5;
        if (bar < 0)  bar = 0;
        if (bar > 20) bar = 20;

        // Print one line per frequency step
        Serial1.print("  ");
        Serial1.print(freq / 1e6f, 3);
        Serial1.print("   ");
        if (rssi > -100) Serial1.print(' ');
        if (rssi > -10)  Serial1.print(' ');
        Serial1.print(rssi);
        Serial1.print(" dBm  ");
        for (int i = 0; i < bar; i++) Serial1.print('#');
        if (active) Serial1.print("  << SIGNAL");
        Serial1.println();

        // On active channels, listen briefly for a real packet (50 ms)
        if (active) {
            PacketInfo pkt;
            uint32_t listenStart = millis();
            while (millis() - listenStart < 50) {
                if (radio.checkForPacket(pkt)) {
                    Serial1.print("            ^^ PACKET ");
                    Serial1.print(pkt.length);
                    Serial1.print("B  RSSI:");
                    Serial1.print(pkt.rssi);
                    Serial1.print("  SNR:");
                    Serial1.print((float)pkt.snr / 4.0f, 1);
                    Serial1.println(" dB");
                    hexDump(pkt.data, pkt.length);
                    break;
                }
            }
        }

        radio.goStandby();
    }

    // Band summary
    Serial1.print("  >> Peak: ");
    Serial1.print(peakFreq / 1e6f, 3);
    Serial1.print(" MHz @ ");
    Serial1.print(peakRssi);
    Serial1.print(" dBm   Active: ");
    Serial1.print(activeCount); Serial1.print("/"); Serial1.println(totalSteps);

    return true;
}

/**
 * Full frequency scan command.
 * Default (no args): scans ISM 433 / 868 / 915 MHz bands.
 * Custom range:      scan <startMHz> <endMHz> [stepKHz]
 */
static void cmdScan(const char* arg) {
    // Save current frequency so we can restore it afterwards
    uint32_t savedFreq = radio.getConfig().frequencyHz;

    // Stop any active mode before scanning
    if (appMode != MODE_IDLE) {
        radio.goStandby();
        appMode = MODE_IDLE;
    }

    Serial1.println();
    Serial1.println("[SCAN] Starting frequency scan...");
    Serial1.println("       Press 'q' or Ctrl+C to abort.");

    uint32_t t0 = millis();
    bool completed = true;

    if (*arg == '\0' || strcasecmp(arg, "all") == 0) {
        // Default: scan the three main ISM bands
        completed = scanFreqRange(430.0f, 440.0f, 200.0f, "ISM 433 MHz");
        if (completed) completed = scanFreqRange(863.0f, 870.0f, 200.0f, "ISM 868 MHz");
        if (completed) completed = scanFreqRange(902.0f, 928.0f, 500.0f, "ISM 915 MHz");
    } else if (strcasecmp(arg, "433") == 0) {
        completed = scanFreqRange(430.0f, 440.0f, 100.0f, "ISM 433 MHz");
    } else if (strcasecmp(arg, "868") == 0) {
        completed = scanFreqRange(863.0f, 870.0f, 100.0f, "ISM 868 MHz");
    } else if (strcasecmp(arg, "915") == 0) {
        completed = scanFreqRange(902.0f, 928.0f, 250.0f, "ISM 915 MHz");
    } else {
        // Parse: scan <startMHz> <endMHz> [stepKHz]
        char* p1;
        float startMHz = strtof(arg, &p1);
        while (*p1 == ' ') p1++;
        char* p2;
        float endMHz = strtof(p1, &p2);
        while (*p2 == ' ') p2++;
        float stepKHz = 200.0f;   // default step if omitted
        if (*p2 != '\0') stepKHz = strtof(p2, nullptr);

        // Validate
        if (startMHz < 150.0f || endMHz > 960.0f ||
            startMHz >= endMHz || stepKHz < 1.0f) {
            Serial1.println("[ERR] Usage: scan <startMHz> <endMHz> [stepKHz]");
            Serial1.println("       Range 150-960 MHz, step >= 1 kHz");
            radio.setFrequency(savedFreq);
            return;
        }

        completed = scanFreqRange(startMHz, endMHz, stepKHz, "Custom");
    }

    uint32_t elapsed = millis() - t0;
    Serial1.println();
    Serial1.print("[SCAN] ");
    Serial1.print(completed ? "Complete" : "Aborted");
    Serial1.print(" in ");
    Serial1.print(elapsed);
    Serial1.println(" ms");

    // Restore the original operating frequency
    radio.setFrequency(savedFreq);
    Serial1.print("[SCAN] Restored freq: ");
    Serial1.print(savedFreq / 1e6f, 3);
    Serial1.println(" MHz");
}

/** Quick preset scan command for ISM bands */
static void cmdScanPreset(const char* arg) {
    if (*arg == '\0') {
        Serial1.println("[ERR] Usage: scanpreset <433|868|915|all>");
        return;
    }
    cmdScan(arg);
}

/** Enter live analyzer mode (redrawn RSSI graph in terminal). */
static void cmdAnalyze(const char* arg) {
    uint32_t startHz = 0, endHz = 0, stepHz = 0;
    if (!parseAnalyzeArgs(arg, startHz, endHz, stepHz)) {
        Serial1.println("[ERR] Usage: analyze [startMHz endMHz [stepKHz]]");
        Serial1.println("      Example: analyze 432 434 25");
        return;
    }

    if (appMode != MODE_IDLE) radio.goStandby();

    analyzePrevFreq = radio.getConfig().frequencyHz;
    analyzeStartHz = startHz;
    analyzeEndHz = endHz;
    analyzeStepHz = stepHz;
    analyzeNeedsClear = true;
    analyzeHoldPeakRssi = -200;
    analyzeHoldPeakFreq = analyzeStartHz;
    lastAnalyzeFrame = 0;
    appMode = MODE_ANALYZE;
}

/** Configure analyzer options: peak/threshold */
static void cmdAnalyzeCfg(const char* arg) {
    char key[16] = {};
    char val[16] = {};
    int n = sscanf(arg, "%15s %15s", key, val);
    if (n < 2) {
        Serial1.println("[ERR] Usage: analyzecfg <peak|threshold> <value>");
        return;
    }

    if (strcasecmp(key, "peak") == 0) {
        if (strcasecmp(val, "on") == 0) analyzePeakHold = true;
        else if (strcasecmp(val, "off") == 0) analyzePeakHold = false;
        else { Serial1.println("[ERR] analyzecfg peak <on|off>"); return; }
        Serial1.print("[CFG] Analyze peak hold: "); Serial1.println(analyzePeakHold ? "ON" : "OFF");
    } else if (strcasecmp(key, "threshold") == 0) {
        int t = atoi(val);
        if (t < -130 || t > -20) {
            Serial1.println("[ERR] analyzecfg threshold <-130..-20>");
            return;
        }
        analyzeThresholdDbm = (int16_t)t;
        Serial1.print("[CFG] Analyze threshold: "); Serial1.println(analyzeThresholdDbm);
    } else {
        Serial1.println("[ERR] Unknown analyzecfg key");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Per-Command Help System
// ═════════════════════════════════════════════════════════════════════════════

/** Show detailed help for a specific command */
static void showCmdHelp(const char* cmd) {
    Serial1.println();
    if (strcasecmp(cmd, "send") == 0) {
        Serial1.println("send <message>");
        Serial1.println("Transmits ASCII text as one packet.");
        Serial1.println("If sniff is active, RX resumes automatically after TX.");
    } else if (strcasecmp(cmd, "sendhex") == 0) {
        Serial1.println("sendhex <hexbytes>");
        Serial1.println("Transmits raw bytes. Two hex chars = one byte.");
        Serial1.println("Example: sendhex 48656C6C6F");
    } else if (strcasecmp(cmd, "sniff") == 0) {
        Serial1.println("sniff");
        Serial1.println("Continuous receive monitor with RSSI/SNR and payload dump.");
    } else if (strcasecmp(cmd, "beacon") == 0) {
        Serial1.println("beacon <interval_ms> <message>");
        Serial1.println("Sends the same message periodically until stopped.");
    } else if (strcasecmp(cmd, "stop") == 0) {
        Serial1.println("stop");
        Serial1.println("Stops active mode: sniff, beacon, analyze, or chat.");
    } else if (strcasecmp(cmd, "scan") == 0) {
        Serial1.println("scan [startMHz endMHz stepKHz] | 433 | 868 | 915 | all");
        Serial1.println("Examples: scan 430 440 50, scan 433, scan all");
    } else if (strcasecmp(cmd, "scanpreset") == 0) {
        Serial1.println("scanpreset <433|868|915|all>");
    } else if (strcasecmp(cmd, "meshlisten") == 0) {
        Serial1.println("meshlisten <longfast|longslow|shortfast> <freqMHz>");
        Serial1.println("Applies Meshtastic-like LoRa PHY settings and starts sniff.");
        Serial1.println("Shows raw dump plus basic Meshtastic protobuf field summary.");
    } else if (strcasecmp(cmd, "analyze") == 0) {
        Serial1.println("analyze [startMHz endMHz stepKHz] | 433 | 868 | 915");
        Serial1.println("Shows per-frequency RSSI table, threshold markers, and peaks.");
        Serial1.println("Examples: analyze, analyze 433, analyze 432 434 25");
    } else if (strcasecmp(cmd, "analyzecfg") == 0) {
        Serial1.println("analyzecfg peak on|off");
        Serial1.println("analyzecfg threshold -95");
        Serial1.println("Threshold range: -130..-20 dBm");
    } else if (strcasecmp(cmd, "modem") == 0) {
        Serial1.println("modem <lora|gfsk>");
        Serial1.println("Switches packet type and applies modem-specific params.");
    } else if (strcasecmp(cmd, "bitrate") == 0) {
        Serial1.println("bitrate <600..300000>");
    } else if (strcasecmp(cmd, "fdev") == 0) {
        Serial1.println("fdev <100..200000>");
    } else if (strcasecmp(cmd, "fskbw") == 0) {
        Serial1.println("fskbw <hexcode>");
    } else if (strcasecmp(cmd, "whitening") == 0) {
        Serial1.println("whitening <on|off>");
    } else if (strcasecmp(cmd, "header") == 0) {
        Serial1.println("header <explicit|implicit [len]>");
    } else if (strcasecmp(cmd, "ldro") == 0) {
        Serial1.println("ldro <auto|on|off>");
    } else if (strcasecmp(cmd, "symtimeout") == 0) {
        Serial1.println("symtimeout <0..255>");
    } else if (strcasecmp(cmd, "rxboost") == 0) {
        Serial1.println("rxboost <on|off>");
    } else if (strcasecmp(cmd, "standby") == 0) {
        Serial1.println("standby <xosc|rc>");
    } else if (strcasecmp(cmd, "regulator") == 0) {
        Serial1.println("regulator <dcdc|ldo>");
    } else if (strcasecmp(cmd, "syncword") == 0) {
        Serial1.println("syncword <hex>");
        Serial1.println("Common values: 12 (private), 34 (public)");
    } else if (strcasecmp(cmd, "freq") == 0) {
        Serial1.println("freq <MHz>");
    } else if (strcasecmp(cmd, "sf") == 0) {
        Serial1.println("sf <5..12>");
    } else if (strcasecmp(cmd, "bw") == 0) {
        Serial1.println("bw <kHz>");
    } else if (strcasecmp(cmd, "cr") == 0) {
        Serial1.println("cr <5..8>");
    } else if (strcasecmp(cmd, "power") == 0) {
        Serial1.println("power <-9..22>");
    } else if (strcasecmp(cmd, "preamble") == 0) {
        Serial1.println("preamble <1..65535>");
    } else if (strcasecmp(cmd, "crc") == 0 || strcasecmp(cmd, "iq") == 0) {
        Serial1.print(cmd); Serial1.println(" <on|off>");
    } else if (strcasecmp(cmd, "send") == 0) {
        Serial1.println("send <message>");
        Serial1.println("Sends ASCII text as payload.");
    } else if (strcasecmp(cmd, "sendhex") == 0) {
        Serial1.println("sendhex <hexbytes>");
        Serial1.println("Each two hex chars are one payload byte.");
    } else if (strcasecmp(cmd, "beacon") == 0) {
        Serial1.println("beacon <interval_ms> <message>");
    } else if (strcasecmp(cmd, "chatjoin") == 0) {
        Serial1.println("chatjoin <room 0..255> [hexkey]");
        Serial1.println("Join encrypted room and enter chat listen mode.");
        Serial1.println("If key is omitted, a key is auto-generated and printed.");
        Serial1.println("Provided key can be 8..32 hex chars (even length).");
        Serial1.println("All nodes must use the same room and effective key.");
    } else if (strcasecmp(cmd, "chatleave") == 0) {
        Serial1.println("chatleave");
        Serial1.println("Leaves room and clears key material from RAM.");
    } else if (strcasecmp(cmd, "chatnick") == 0) {
        Serial1.println("chatnick <name>");
        Serial1.println("Sets display nickname for chat messages.");
    } else if (strcasecmp(cmd, "chat") == 0) {
        Serial1.println("chat <message>");
        Serial1.println("Encrypts and sends chat message to joined room.");
        Serial1.println("In chat mode, you can also type message text directly.");
    } else if (strcasecmp(cmd, "chatstatus") == 0) {
        Serial1.println("chatstatus");
        Serial1.println("Shows joined state, room, nickname, and sender ID.");
    } else if (strcasecmp(cmd, "status") == 0) {
        Serial1.println("status");
        Serial1.println("Prints full modem and runtime configuration.");
    } else if (strcasecmp(cmd, "rssi") == 0) {
        Serial1.println("rssi");
        Serial1.println("Reads instantaneous RSSI on current channel.");
    } else if (strcasecmp(cmd, "reset") == 0) {
        Serial1.println("reset");
        Serial1.println("Re-initializes the radio with current frequency.");
    } else if (strcasecmp(cmd, "uptime") == 0) {
        Serial1.println("uptime");
        Serial1.println("Shows MCU uptime in days/hours/minutes/seconds.");
    } else if (strcasecmp(cmd, "version") == 0 || strcasecmp(cmd, "ver") == 0) {
        Serial1.println("version | ver");
        Serial1.println("Shows firmware version and board information.");
    } else if (strcasecmp(cmd, "sleep") == 0) {
        Serial1.println("sleep");
        Serial1.println("Puts radio into standby (low power, MCU still active).");
    } else if (strcasecmp(cmd, "clear") == 0 || strcasecmp(cmd, "cls") == 0) {
        Serial1.println("clear | cls");
        Serial1.println("Clears terminal screen with ANSI escape codes.");
    } else if (strcasecmp(cmd, "reboot") == 0) {
        Serial1.println("reboot");
        Serial1.println("Software reset of MCU.");
    } else if (strcasecmp(cmd, "help") == 0) {
        Serial1.println("help");
        Serial1.println("Shows command groups and quick usage tips.");
    } else {
        Serial1.println("See: help");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Command Router
// ═════════════════════════════════════════════════════════════════════════════

/** Parse a line of serial input and route to the appropriate handler */
static void processCommand(char* line) {
    char originalLine[sizeof(cmdBuf)] = {};
    strncpy(originalLine, line, sizeof(originalLine) - 1);

    // Skip leading whitespace
    while (*line == ' ') line++;
    if (*line == '\0') return;

    // Split into command and argument at the first space
    char* arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg = '\0'; arg++; }   // Null-terminate command word
    while (*arg == ' ') arg++;           // Skip spaces before argument

    // Check for -help / --help / -h flag on any command
    if (strcmp(arg, "-help") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        showCmdHelp(line);
        prompt();
        return;
    }

    // Dispatch to the correct handler (case-insensitive)
    if      (strcasecmp(line, "help")     == 0) cmdHelp();
    else if (strcasecmp(line, "send")     == 0) cmdSend(arg);
    else if (strcasecmp(line, "sendhex")  == 0) cmdSendHex(arg);
    else if (strcasecmp(line, "sniff")    == 0) cmdSniff();
    else if (strcasecmp(line, "beacon")   == 0) cmdBeacon(arg);
    else if (strcasecmp(line, "stop")     == 0) cmdStop();
    else if (strcasecmp(line, "freq")     == 0) cmdFreq(arg);
    else if (strcasecmp(line, "sf")       == 0) cmdSF(arg);
    else if (strcasecmp(line, "bw")       == 0) cmdBW(arg);
    else if (strcasecmp(line, "cr")       == 0) cmdCR(arg);
    else if (strcasecmp(line, "power")    == 0) cmdPower(arg);
    else if (strcasecmp(line, "preamble") == 0) cmdPreamble(arg);
    else if (strcasecmp(line, "modem")    == 0) cmdModem(arg);
    else if (strcasecmp(line, "bitrate")  == 0) cmdBitrate(arg);
    else if (strcasecmp(line, "fdev")     == 0) cmdFdev(arg);
    else if (strcasecmp(line, "fskbw")    == 0) cmdFskBw(arg);
    else if (strcasecmp(line, "whitening") == 0) cmdWhitening(arg);
    else if (strcasecmp(line, "header")   == 0) cmdHeader(arg);
    else if (strcasecmp(line, "ldro")     == 0) cmdLdro(arg);
    else if (strcasecmp(line, "symtimeout") == 0) cmdSymTimeout(arg);
    else if (strcasecmp(line, "rxboost")  == 0) cmdRxBoost(arg);
    else if (strcasecmp(line, "standby")  == 0) cmdStandby(arg);
    else if (strcasecmp(line, "regulator") == 0) cmdRegulator(arg);
    else if (strcasecmp(line, "crc")      == 0) cmdCrc(arg);
    else if (strcasecmp(line, "iq")       == 0) cmdIQ(arg);
    else if (strcasecmp(line, "syncword") == 0) cmdSyncWord(arg);
    else if (strcasecmp(line, "status")   == 0) cmdStatus();
    else if (strcasecmp(line, "rssi")     == 0) cmdRssi();
    else if (strcasecmp(line, "reset")    == 0) cmdReset();
    else if (strcasecmp(line, "scan")     == 0) cmdScan(arg);
    else if (strcasecmp(line, "scanpreset") == 0) cmdScanPreset(arg);
    else if (strcasecmp(line, "meshlisten") == 0) cmdMeshListen(arg);
    else if (strcasecmp(line, "analyze")  == 0) cmdAnalyze(arg);
    else if (strcasecmp(line, "analyzecfg") == 0) cmdAnalyzeCfg(arg);
    else if (strcasecmp(line, "chatjoin") == 0) cmdChatJoin(arg);
    else if (strcasecmp(line, "chatleave") == 0) cmdChatLeave();
    else if (strcasecmp(line, "chatnick") == 0) cmdChatNick(arg);
    else if (strcasecmp(line, "chat") == 0) cmdChatSend(arg);
    else if (strcasecmp(line, "chatstatus") == 0) cmdChatStatus();
    // System commands
    else if (strcasecmp(line, "clear")    == 0) cmdClear();
    else if (strcasecmp(line, "cls")      == 0) cmdClear();
    else if (strcasecmp(line, "uptime")   == 0) cmdUptime();
    else if (strcasecmp(line, "version")  == 0) cmdVersion();
    else if (strcasecmp(line, "ver")      == 0) cmdVersion();
    else if (strcasecmp(line, "sleep")    == 0) cmdSleep();
    else if (strcasecmp(line, "reboot")   == 0) cmdReboot();
    else {
        if (appMode == MODE_CHAT && chatJoined) {
            cmdChatSend(originalLine);
        } else {
            Serial1.print("[ERR] Unknown command: ");
            Serial1.println(line);
            Serial1.println("      Type 'help' for a list of commands");
        }
    }

    prompt();
}

/** Read incoming serial characters and build up a command line */
static void readSerial() {
    while (Serial1.available()) {
        char c = Serial1.read();

        // Newline → process the accumulated line
        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = '\0';
                processCommand(cmdBuf);
                cmdLen = 0;
            }
        }
        // Ctrl+C → stop active mode
        else if (c == 0x03) {
            if (appMode != MODE_IDLE) {
                Serial1.println("^C");
                cmdStop();
                cmdLen = 0;
                prompt();
            } else {
                Serial1.println("^C");
                cmdLen = 0;
                prompt();
            }
        }
        // Quick analyzer stop with q
        else if ((c == 'q' || c == 'Q') && appMode == MODE_ANALYZE) {
            cmdStop();
            cmdLen = 0;
            prompt();
        }
        // Ctrl+L → clear screen
        else if (c == 0x0C) {
            Serial1.print("\033[2J\033[H");
            prompt();
        }
        // Backspace handling
        else if (c == '\b' || c == 0x7F) {
            if (cmdLen > 0) {
                cmdLen--;
                Serial1.print("\b \b");  // Erase character on terminal
            }
        }
        // Normal character → append to buffer
        else if (cmdLen < sizeof(cmdBuf) - 1) {
            cmdBuf[cmdLen++] = c;
            Serial1.print(c);  // Echo
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Arduino Setup & Main Loop
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
    // LED setup — immediate visual feedback
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);    // OFF (active low)

    // Brief startup blink to confirm firmware is running
    for (int i = 0; i < 2; i++) {
        digitalWrite(LED_PIN, LOW);  delay(80);   // ON
        digitalWrite(LED_PIN, HIGH); delay(80);   // OFF
    }

    // Serial port (UART1: PA9=TX, PA10=RX)
    Serial1.begin(115200);
    delay(200);

    // Welcome banner
    Serial1.println("\n\n");
    Serial1.println("+=============================================+");
    Serial1.println("|  STM32 LoRa Terminal  v1.0                  |");
    Serial1.println("|  SX1262 (DX-LR30) @ "); Serial1.print(SystemCoreClock / 1000000); Serial1.println(" MHz        |");
    Serial1.println("|  Type 'help' for commands                   |");
    Serial1.println("+=============================================+");

    // Initialise the radio with default frequency
    Serial1.println("[INIT] Starting radio...");
    if (!radio.begin(915000000)) {
        Serial1.println("[ERR] Radio init failed!  Halting.");
        while (true) {
            digitalWrite(LED_PIN, LOW);  delay(100);
            digitalWrite(LED_PIN, HIGH); delay(100);
        }
    }
    Serial1.println("[INIT] Radio OK!");
    chatSenderId = deviceId16();

    // Brief LED flash to show we're alive
    digitalWrite(LED_PIN, LOW);  delay(200);
    digitalWrite(LED_PIN, HIGH);

    prompt();
}

void loop() {
    // ── Always process serial input ─────────────────────────────────────────
    readSerial();

    // ── Sniff mode: poll for packets and print periodic status ──────────────
    if (appMode == MODE_SNIFFING) {
        PacketInfo pkt;
        if (radio.checkForPacket(pkt)) {
            // Brief LED blink on packet
            digitalWrite(LED_PIN, LOW);
            displayPacket(pkt);
            digitalWrite(LED_PIN, HIGH);
        }

        // Print an activity heartbeat every 10 seconds if no packets
        if (millis() - lastSniffStatus >= 10000) {
            lastSniffStatus = millis();
            Serial1.print("[SNIFF] ");
            Serial1.print(millis() / 1000);
            Serial1.print("s  Listening...  RSSI: ");
            Serial1.print(radio.readRssi());
            Serial1.print(" dBm  Pkts: ");
            Serial1.println(radio.getPacketCount());
        }
    }

    // ── Beacon mode: periodic transmit ──────────────────────────────────────
    if (appMode == MODE_BEACONING && millis() - lastBeaconTime >= beaconInterval) {
        lastBeaconTime = millis();
        digitalWrite(LED_PIN, LOW);

        uint32_t t0 = millis();
        bool ok = radio.send((const uint8_t*)beaconMsg, beaconLen);
        uint32_t elapsed = millis() - t0;

        digitalWrite(LED_PIN, HIGH);

        if (ok) {
            Serial1.print("[BEACON] TX \"");
            Serial1.print(beaconMsg);
            Serial1.print("\" (");
            Serial1.print(elapsed);
            Serial1.println(" ms)");
        } else {
            Serial1.println("[BEACON] TX FAILED");
        }
    }

    // ── Analyze mode: redraw live graph periodically ───────────────────────
    if (appMode == MODE_ANALYZE && millis() - lastAnalyzeFrame >= 350) {
        lastAnalyzeFrame = millis();
        renderAnalyzeFrame();
    }

    // ── Chat mode: receive encrypted chat packets ──────────────────────────
    if (appMode == MODE_CHAT) {
        PacketInfo pkt;
        if (radio.checkForPacket(pkt)) {
            char nick[16] = {};
            char msg[128] = {};
            if (parseChatPacket(pkt.data, pkt.length, nick, sizeof(nick), msg, sizeof(msg))) {
                Serial1.print(nick);
                Serial1.print(" :> \"");
                Serial1.print(msg);
                Serial1.println("\"");
                digitalWrite(LED_PIN, LOW);
                delay(15);
                digitalWrite(LED_PIN, HIGH);
            }
        }

        if (millis() - lastChatStatus >= 15000) {
            lastChatStatus = millis();
            Serial1.print("[CHAT] Listening room ");
            Serial1.print(chatRoomId);
            Serial1.print(" RSSI: ");
            Serial1.print(radio.readRssi());
            Serial1.println(" dBm");
        }
    }

    // Small yield to avoid tight-loop starvation
    delay(1);
}
