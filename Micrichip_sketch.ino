#include <Wire.h>


#define DEVICE_ADDR 0x60

#define AES_SLOT 8


/* ============================================================
 * WORD ADDRESSES
 * ============================================================ */

#define WORD_ADDR_IDLE     0x02
#define WORD_ADDR_COMMAND  0x03


/* ============================================================
 * OPCODES
 * ============================================================ */

#define OPCODE_READ   0x02
#define OPCODE_WRITE  0x12
#define OPCODE_LOCK   0x17
#define OPCODE_AES    0x51


/* ============================================================
 * CONFIGURATION BUFFER
 * ============================================================ */

uint8_t configZone[128];


/* ============================================================
 * AES KEY
 *
 * TEST / DEVELOPMENT KEY ONLY
 *
 * 9ee74f11306b86fe5f4b8e91f0ea0198
 * ============================================================ */

const uint8_t aesKey[16] =
{
    0x9E, 0xE7, 0x4F, 0x11,
    0x30, 0x6B, 0x86, 0xFE,
    0x5F, 0x4B, 0x8E, 0x91,
    0xF0, 0xEA, 0x01, 0x98
};


/* ============================================================
 * 16 BYTE PLAINTEXT TEST
 *
 * "ATECC608B-AES-01"
 * ============================================================ */

const uint8_t testPlaintext[16] =
{
    'A','T','E','C','C','6','0','8',
    'B','-','A','E','S','-','0','1'
};



bool waitForStatusResponse(uint16_t timeoutMs)
{
    unsigned long start = millis();

    while ((millis() - start) < timeoutMs)
    {
        uint8_t received =
            Wire.requestFrom(
                DEVICE_ADDR,
                (uint8_t)4
            );

        if (received == 4)
        {
            uint8_t response[4];

            for (uint8_t i = 0; i < 4; i++)
            {
                response[i] = Wire.read();
            }

            Serial.print(F("ATECC response: "));
            printBuffer(response, 4);

            if (!checkCRC(response, 4))
            {
                Serial.println(F("Response CRC FAILED"));
                return false;
            }

            if (response[0] != 0x04)
            {
                Serial.println(F("Invalid response count"));
                return false;
            }

            if (response[1] == 0x00)
            {
                Serial.println(F("Command SUCCESS."));
                return true;
            }

            Serial.print(F("ATECC status/error = 0x"));
            printHex(response[1]);
            Serial.println();

            return false;
        }

        /*
         * Device may still be busy executing the command.
         * Do NOT wake it while busy.
         */
        delay(2);
    }

    Serial.println(F("ERROR: Timeout waiting for ATECC response."));
    return false;
}

void sleepChip()
{
    Wire.beginTransmission(DEVICE_ADDR);
    Wire.write(0x01);   // Sleep
    Wire.endTransmission();

    delay(2);
}

bool prepareChip()
{
    /*
     * Put the device into a known state.
     *
     * If it is already asleep, this transmission may NACK.
     * That's harmless.
     */
    sleepChip();

    delay(2);

    Serial.println(F("Waking chip for command..."));

    if (!wakeChip())
    {
        Serial.println(F("ERROR: Cannot wake ATECC608B"));
        return false;
    }

    return true;
}

/* ============================================================
 * CRC
 * ============================================================ */

void calculateCRC(
    uint8_t length,
    const uint8_t *data,
    uint8_t *crcOut)
{
    uint16_t crcRegister = 0;

    const uint16_t polynom = 0x8005;


    for (uint8_t counter = 0;
         counter < length;
         counter++)
    {
        for (uint8_t shiftReg = 0x01;
             shiftReg != 0;
             shiftReg <<= 1)
        {
            uint8_t dataBit =
                (data[counter] & shiftReg)
                ? 1
                : 0;


            uint8_t crcBit =
                (uint8_t)(crcRegister >> 15);


            crcRegister <<= 1;


            if (dataBit != crcBit)
            {
                crcRegister ^= polynom;
            }
        }
    }


    crcOut[0] =
        (uint8_t)(crcRegister & 0xFF);

    crcOut[1] =
        (uint8_t)(crcRegister >> 8);
}


/* ============================================================
 * PRINT HELPERS
 * ============================================================ */

void printHex(uint8_t value)
{
    if (value < 0x10)
    {
        Serial.print('0');
    }


    Serial.print(
        value,
        HEX
    );
}


void printBuffer(
    const uint8_t *data,
    uint8_t length)
{
    for (uint8_t i = 0;
         i < length;
         i++)
    {
        printHex(
            data[i]
        );


        if (i != length - 1)
        {
            Serial.print(' ');
        }
    }


    Serial.println();
}


/* ============================================================
 * CHECK CRC
 * ============================================================ */

bool checkCRC(
    const uint8_t *response,
    uint8_t length)
{
    if (length < 3)
    {
        return false;
    }


    uint8_t crc[2];


    calculateCRC(
        length - 2,
        response,
        crc
    );


    return
        response[length - 2] == crc[0] &&
        response[length - 1] == crc[1];
}


/* ============================================================
 * WAKE
 * ============================================================ */

bool wakeChip()
{
    Wire.beginTransmission(
        0x00
    );

    Wire.write(
        0x00
    );

    Wire.endTransmission();


    delayMicroseconds(
        1500
    );


    uint8_t received =
        Wire.requestFrom(
            DEVICE_ADDR,
            (uint8_t)4
        );


    if (received != 4)
    {
        Serial.print(
            F("Wake response length = ")
        );

        Serial.println(
            received
        );

        return false;
    }


    uint8_t response[4];


    for (uint8_t i = 0;
         i < 4;
         i++)
    {
        response[i] =
            Wire.read();
    }


    Serial.print(
        F("Wake: ")
    );

    printBuffer(
        response,
        4
    );


    if (!checkCRC(
            response,
            4))
    {
        Serial.println(
            F("Wake CRC FAILED")
        );

        return false;
    }


    if (
        response[0] != 0x04 ||
        response[1] != 0x11
    )
    {
        Serial.println(
            F("Invalid wake response")
        );

        return false;
    }


    return true;
}


/* ============================================================
 * READ CONFIG WORD
 *
 * word 0  = bytes 0..3
 * word 1  = bytes 4..7
 * ...
 * word 31 = bytes 124..127
 * ============================================================ */

bool readConfigWord(
    uint8_t wordAddr,
    uint8_t *data4)
{
    uint8_t packet[5];


    packet[0] = 0x07;
    packet[1] = OPCODE_READ;

    /* Configuration zone, 4-byte access */

    packet[2] = 0x00;

    packet[3] = wordAddr;
    packet[4] = 0x00;


    uint8_t crc[2];


    calculateCRC(
        sizeof(packet),
        packet,
        crc
    );


    Wire.beginTransmission(
        DEVICE_ADDR
    );


    Wire.write(
        WORD_ADDR_COMMAND
    );


    Wire.write(
        packet,
        sizeof(packet)
    );


    Wire.write(
        crc,
        2
    );


    uint8_t tx =
        Wire.endTransmission();


    if (tx != 0)
    {
        Serial.print(
            F("Config Read I2C error = ")
        );

        Serial.println(
            tx
        );

        return false;
    }


    delay(5);


    uint8_t received =
        Wire.requestFrom(
            DEVICE_ADDR,
            (uint8_t)7
        );


    if (received != 7)
    {
        Serial.print(
            F("Config response length = ")
        );

        Serial.println(
            received
        );

        return false;
    }


    uint8_t response[7];


    for (uint8_t i = 0;
         i < 7;
         i++)
    {
        response[i] =
            Wire.read();
    }


    if (response[0] != 0x07)
    {
        Serial.println(
            F("Bad Config response count")
        );

        return false;
    }


    if (!checkCRC(
            response,
            7))
    {
        Serial.println(
            F("Config response CRC FAILED")
        );

        return false;
    }


    for (uint8_t i = 0;
         i < 4;
         i++)
    {
        data4[i] =
            response[i + 1];
    }


    return true;
}


/* ============================================================
 * READ COMPLETE CONFIG
 * ============================================================ */

bool readConfig()
{
    for (uint8_t word = 0;
         word < 32;
         word++)
    {
        if (!readConfigWord(
                word,
                &configZone[word * 4]))
        {
            Serial.print(
                F("Config read FAILED at word 0x")
            );

            Serial.println(
                word,
                HEX
            );

            return false;
        }
    }


    return true;
}


/* ============================================================
 * STATUS RESPONSE
 * ============================================================ */

bool readStatusResponse()
{
    uint8_t received =
        Wire.requestFrom(
            DEVICE_ADDR,
            (uint8_t)4
        );


    if (received != 4)
    {
        Serial.print(
            F("Status response length = ")
        );

        Serial.println(
            received
        );

        return false;
    }


    uint8_t response[4];


    for (uint8_t i = 0;
         i < 4;
         i++)
    {
        response[i] =
            Wire.read();
    }


    Serial.print(
        F("ATECC response: ")
    );


    printBuffer(
        response,
        4
    );


    if (!checkCRC(
            response,
            4))
    {
        Serial.println(
            F("Response CRC FAILED")
        );

        return false;
    }


    if (response[0] != 0x04)
    {
        Serial.println(
            F("Invalid status response count")
        );

        return false;
    }


    if (response[1] != 0x00)
    {
        Serial.print(
            F("ATECC status/error = 0x")
        );

        printHex(
            response[1]
        );

        Serial.println();

        return false;
    }


    return true;
}


/* ============================================================
 * WRITE 4-BYTE CONFIG WORD
 *
 * ONLY works while Configuration zone is unlocked.
 * ============================================================ */

bool writeConfigWord(
    uint8_t wordAddr,
    const uint8_t data[4])
{
    uint8_t packet[9];


    /*
     * Count:
     *
     * Count     1
     * Opcode    1
     * Param1    1
     * Param2    2
     * Data      4
     * CRC       2
     *
     * total = 11 = 0x0B
     */

    packet[0] = 0x0B;

    packet[1] =
        OPCODE_WRITE;


    /* Configuration zone / 4-byte write */

    packet[2] =
        0x00;


    packet[3] =
        wordAddr;

    packet[4] =
        0x00;


    packet[5] =
        data[0];

    packet[6] =
        data[1];

    packet[7] =
        data[2];

    packet[8] =
        data[3];


    uint8_t crc[2];


    calculateCRC(
        sizeof(packet),
        packet,
        crc
    );


    Serial.print(
        F("Writing Config word 0x")
    );

    Serial.print(
        wordAddr,
        HEX
    );

    Serial.print(
        F(" -> ")
    );


    printBuffer(
        data,
        4
    );


    Wire.beginTransmission(
        DEVICE_ADDR
    );


    Wire.write(
        WORD_ADDR_COMMAND
    );


    Wire.write(
        packet,
        sizeof(packet)
    );


    Wire.write(
        crc,
        2
    );


    uint8_t result =
        Wire.endTransmission();


    Serial.print(
        F("Wire.endTransmission = ")
    );

    Serial.println(
        result
    );


    if (result != 0)
    {
        return false;
    }


    delay(30);


    return readStatusResponse();
}


/* ============================================================
 * WRITE + VERIFY CONFIG WORD
 * ============================================================ */

bool writeAndVerifyConfigWord(
    uint8_t wordAddr,
    const uint8_t desired[4])
{
    if (!writeConfigWord(
            wordAddr,
            desired))
    {
        Serial.println(
            F("Config write FAILED")
        );

        return false;
    }


    uint8_t actual[4];


    if (!readConfigWord(
            wordAddr,
            actual))
    {
        Serial.println(
            F("Config read-back FAILED")
        );

        return false;
    }


    Serial.print(
        F("Read-back: ")
    );


    printBuffer(
        actual,
        4
    );


    for (uint8_t i = 0;
         i < 4;
         i++)
    {
        if (actual[i] != desired[i])
        {
            Serial.println(
                F("CONFIG VERIFY FAILED")
            );

            return false;
        }
    }


    Serial.println(
        F("Config word verified.")
    );


    return true;
}


/* ============================================================
 * CONFIG ACCESSORS
 * ============================================================ */

uint16_t getSlotConfig8()
{
    return
        (uint16_t)configZone[36] |
        ((uint16_t)configZone[37] << 8);
}


uint16_t getKeyConfig8()
{
    return
        (uint16_t)configZone[112] |
        ((uint16_t)configZone[113] << 8);
}


uint16_t getSlotLocked()
{
    return
        (uint16_t)configZone[88] |
        ((uint16_t)configZone[89] << 8);
}


bool isAESEnabled()
{
    return
        (configZone[13] & 0x01) != 0;
}


/* ============================================================
 * PRINT CONFIG
 * ============================================================ */

void printConfiguration()
{
    Serial.println();


    Serial.println(
        F("Current configuration:")
    );


    Serial.print(
        F("AES_Enable byte = 0x")
    );

    printHex(
        configZone[13]
    );

    Serial.print(
        F(" -> ")
    );

    Serial.println(
        isAESEnabled()
        ? F("ENABLED")
        : F("DISABLED")
    );


    Serial.print(
        F("LockConfig      = 0x")
    );

    printHex(
        configZone[87]
    );

    Serial.println();


    Serial.print(
        F("LockValue       = 0x")
    );

    printHex(
        configZone[86]
    );

    Serial.println();


    Serial.print(
        F("SlotConfig[8]   = 0x")
    );

    Serial.println(
        getSlotConfig8(),
        HEX
    );


    Serial.print(
        F("KeyConfig[8]    = 0x")
    );

    Serial.println(
        getKeyConfig8(),
        HEX
    );


    Serial.print(
        F("SlotLocked      = 0x")
    );

    Serial.println(
        getSlotLocked(),
        HEX
    );


    Serial.println();
}


/* ============================================================
 * CONFIGURE REQUIRED VALUES
 *
 * This function ONLY runs while LockConfig == 0x55.
 *
 * Important:
 *
 * Configuration writes must be complete 4-byte words.
 *
 * Therefore neighbouring bytes are preserved.
 * ============================================================ */

bool configureRequiredBytes()
{
    if (configZone[87] != 0x55)
    {
        Serial.println(
            F("Configuration already locked.")
        );

        return false;
    }


    if (configZone[86] != 0x55)
    {
        Serial.println(
            F("STOP: Data zone already locked unexpectedly.")
        );

        return false;
    }


    Serial.println();

    Serial.println(
        F("================================")
    );

    Serial.println(
        F("CONFIGURING REQUIRED AES VALUES")
    );

    Serial.println(
        F("================================")
    );


    /*
     * ========================================================
     * 1. AES ENABLE
     *
     * Word 3 = Config bytes 12..15
     *
     * byte 12 = preserve
     * byte 13 = AES_Enable
     * byte 14 = preserve
     * byte 15 = preserve
     *
     * We preserve every other bit in byte 13 and only set bit 0.
     * ========================================================
     */


    if (!isAESEnabled())
    {
        Serial.println();
        Serial.println(
            F("Setting AES_Enable...")
        );


        uint8_t word3[4] =
        {
            configZone[12],

            (uint8_t)(
                configZone[13] |
                0x01
            ),

            configZone[14],
            configZone[15]
        };


        if (!writeAndVerifyConfigWord(
                3,
                word3))
        {
            Serial.println(
                F("STOP: AES_Enable configuration failed.")
            );

            return false;
        }


        configZone[13] =
            word3[1];
    }
    else
    {
        Serial.println(
            F("AES already enabled.")
        );
    }


    /*
     * ========================================================
     * 2. SLOT CONFIG 8
     *
     * bytes 36-37
     *
     * Desired:
     *
     * Config[36] = 8F
     * Config[37] = 0F
     *
     * SlotConfig = 0x0F8F
     *
     * Word 9 contains:
     *
     * bytes 36..39
     *
     * Preserve bytes 38-39.
     * ========================================================
     */


    if (getSlotConfig8() != 0x0F8F)
    {
        Serial.println();
        Serial.println(
            F("Setting SlotConfig[8] = 0x0F8F...")
        );


        uint8_t word9[4] =
        {
            0x8F,
            0x0F,

            configZone[38],
            configZone[39]
        };


        if (!writeAndVerifyConfigWord(
                9,
                word9))
        {
            Serial.println(
                F("STOP: SlotConfig[8] write failed.")
            );

            return false;
        }


        configZone[36] =
            0x8F;

        configZone[37] =
            0x0F;
    }
    else
    {
        Serial.println(
            F("SlotConfig[8] already correct.")
        );
    }


    /*
     * ========================================================
     * 3. SLOT LOCKED
     *
     * bytes 88-89
     *
     * Desired:
     *
     * FF FF
     *
     * Word 22 contains:
     *
     * bytes 88..91
     *
     * Preserve bytes 90-91.
     * ========================================================
     */


    if (getSlotLocked() != 0xFFFF)
    {
        Serial.println();
        Serial.println(
            F("Setting SlotLocked = 0xFFFF...")
        );


        uint8_t word22[4] =
        {
            0xFF,
            0xFF,

            configZone[90],
            configZone[91]
        };


        if (!writeAndVerifyConfigWord(
                22,
                word22))
        {
            Serial.println(
                F("STOP: SlotLocked write failed.")
            );

            return false;
        }


        configZone[88] =
            0xFF;

        configZone[89] =
            0xFF;
    }
    else
    {
        Serial.println(
            F("SlotLocked already 0xFFFF.")
        );
    }


    /*
     * ========================================================
     * 4. KEY CONFIG 8
     *
     * bytes 112-113
     *
     * Desired:
     *
     * 38 00
     *
     * KeyConfig = 0x0038
     *
     * Word 28 contains:
     *
     * bytes 112..115
     *
     * Preserve bytes 114-115.
     * ========================================================
     */


    if (getKeyConfig8() != 0x0038)
    {
        Serial.println();
        Serial.println(
            F("Setting KeyConfig[8] = 0x0038...")
        );


        uint8_t word28[4] =
        {
            0x38,
            0x00,

            configZone[114],
            configZone[115]
        };


        if (!writeAndVerifyConfigWord(
                28,
                word28))
        {
            Serial.println(
                F("STOP: KeyConfig[8] write failed.")
            );

            return false;
        }


        configZone[112] =
            0x38;

        configZone[113] =
            0x00;
    }
    else
    {
        Serial.println(
            F("KeyConfig[8] already correct.")
        );
    }


    /*
     * Read complete configuration again.
     */


    Serial.println();

    Serial.println(
        F("Reading complete configuration after changes...")
    );


    if (!readConfig())
    {
        Serial.println(
            F("STOP: Cannot re-read configuration.")
        );

        return false;
    }


    return true;
}


/* ============================================================
 * VERIFY REQUIRED CONFIGURATION
 * ============================================================ */

bool verifyStaticConfiguration()
{
    bool ok = true;


    Serial.println();

    Serial.println(
        F("================================")
    );

    Serial.println(
        F("VERIFYING AES CONFIGURATION")
    );

    Serial.println(
        F("================================")
    );


    Serial.print(
        F("AES Enable       : ")
    );


    if (isAESEnabled())
    {
        Serial.println(
            F("OK")
        );
    }
    else
    {
        Serial.println(
            F("ERROR")
        );

        ok = false;
    }


    Serial.print(
        F("SlotConfig[8]    : 0x")
    );

    Serial.print(
        getSlotConfig8(),
        HEX
    );


    if (getSlotConfig8() == 0x0F8F)
    {
        Serial.println(
            F(" - OK")
        );
    }
    else
    {
        Serial.println(
            F(" - ERROR")
        );

        ok = false;
    }


    Serial.print(
        F("KeyConfig[8]     : 0x")
    );

    Serial.print(
        getKeyConfig8(),
        HEX
    );


    if (getKeyConfig8() == 0x0038)
    {
        Serial.println(
            F(" - OK")
        );
    }
    else
    {
        Serial.println(
            F(" - ERROR")
        );

        ok = false;
    }


    Serial.print(
        F("SlotLocked       : 0x")
    );

    Serial.print(
        getSlotLocked(),
        HEX
    );


    if (getSlotLocked() == 0xFFFF)
    {
        Serial.println(
            F(" - OK")
        );
    }
    else
    {
        Serial.println(
            F(" - ERROR")
        );

        ok = false;
    }


    if (ok)
    {
        Serial.println();

        Serial.println(
            F("STATIC CONFIGURATION VERIFIED.")
        );
    }


    return ok;
}


/* ============================================================
 * LOCK CONFIG
 * ============================================================ */

bool lockConfigZone()
{

    if (!prepareChip())
        return false;


    uint8_t packet[5];


    packet[0] =
        0x07;

    packet[1] =
        OPCODE_LOCK;


    /*
     * Config zone +
     * ignore Summary CRC
     */

    packet[2] =
        0x80;


    packet[3] =
        0x00;

    packet[4] =
        0x00;


    uint8_t crc[2];


    calculateCRC(
        sizeof(packet),
        packet,
        crc
    );


    Serial.println();

    Serial.println(
        F("*** LOCKING CONFIG ZONE ***")
    );


    Wire.beginTransmission(
        DEVICE_ADDR
    );


    Wire.write(
        WORD_ADDR_COMMAND
    );


    Wire.write(
        packet,
        sizeof(packet)
    );


    Wire.write(
        crc,
        2
    );


    uint8_t result =
        Wire.endTransmission();


    Serial.print(
        F("Wire.endTransmission = ")
    );

    Serial.println(
        result
    );


    if (result != 0)
    {
        return false;
    }


    delay(40);


    return readStatusResponse();
}


/* ============================================================
 * LOCK DATA
 * ============================================================ */

bool lockDataZone()
{
    if (!prepareChip())
    {
        Serial.println(F("Failed to prepare chip for Data lock."));
        return false;
    }

    uint8_t packet[5];

    packet[0] = 0x07;
    packet[1] = OPCODE_LOCK;

    // Data + OTP zone
    // Ignore summary CRC
    packet[2] = 0x81;

    packet[3] = 0x00;
    packet[4] = 0x00;

    uint8_t crc[2];

    calculateCRC(
        sizeof(packet),
        packet,
        crc
    );

    Serial.println();
    Serial.println(F("*** LOCKING DATA ZONE ***"));

    Wire.beginTransmission(DEVICE_ADDR);

    Wire.write(WORD_ADDR_COMMAND);
    Wire.write(packet, sizeof(packet));
    Wire.write(crc, 2);

    uint8_t result =
        Wire.endTransmission();

    Serial.print(F("Wire.endTransmission = "));
    Serial.println(result);

    if (result != 0)
    {
        Serial.println(F("Data Lock I2C transmission FAILED."));
        return false;
    }

    /*
     * Wait for actual ATECC response.
     */
    if (!waitForStatusResponse(200))
    {
        Serial.println(F("Data Lock command did not return SUCCESS."));
        return false;
    }

    /*
     * IMPORTANT:
     * Explicit success return.
     */
    Serial.println(F("DATA LOCK command accepted."));
    return true;
}


/* ============================================================
 * WRITE AES KEY INTO SLOT 8
 *
 * Requires enlarged Arduino UNO Wire buffer.
 *
 * Recommended:
 *
 * BUFFER_LENGTH = 64
 * ============================================================ */

bool writeAESKey()
{
    if (!prepareChip())
        return false;

    uint8_t packet[37];

    /*
     * Count = 39 bytes:
     *
     * 1 Count
     * 1 Opcode
     * 1 Param1
     * 2 Param2
     * 32 Data
     * 2 CRC
     */

    packet[0] = 0x27;
    packet[1] = OPCODE_WRITE;   // 0x12
    packet[2] = 0x82;           // Data zone + 32-byte Write

    /*
     * Slot 8, block 0
     *
     * Address = 0x0040
     */

    packet[3] = 0x40;
    packet[4] = 0x00;


    /*
     * AES-128 key in AES position 0.
     */

    for (uint8_t i = 0; i < 16; i++)
    {
        packet[5 + i] = aesKey[i];
    }


    /*
     * Remaining 16 bytes of required 32-byte write.
     */

    for (uint8_t i = 16; i < 32; i++)
    {
        packet[5 + i] = 0x00;
    }


    uint8_t crc[2];

    calculateCRC(
        sizeof(packet),
        packet,
        crc
    );


    Serial.println();
    Serial.println(F("Writing AES key into Slot 8..."));


    /*
     * ========================================================
     * DIAGNOSTIC: SHOW COMMAND
     * ========================================================
     */

    Serial.print(F("Command packet: "));

    for (uint8_t i = 0; i < sizeof(packet); i++)
    {
        printHex(packet[i]);
        Serial.print(' ');
    }

    Serial.println();


    Serial.print(F("CRC: "));
    printHex(crc[0]);
    Serial.print(' ');
    printHex(crc[1]);
    Serial.println();


    /*
     * With the current development key the CRC should be:
     *
     * 35 6F
     */

    if (
        crc[0] != 0x35 ||
        crc[1] != 0x6F
    )
    {
        Serial.println(F("STOP: Unexpected Write CRC."));
        return false;
    }


    /*
     * ========================================================
     * SEND COMMAND
     *
     * IMPORTANT:
     *
     * We intentionally send ONE BYTE AT A TIME so that the
     * return value from Wire.write() is meaningful.
     * ========================================================
     */

    Wire.beginTransmission(
        DEVICE_ADDR
    );


    uint8_t queued = 0;


    /*
     * ATECC I2C word address.
     */

    if (Wire.write(WORD_ADDR_COMMAND) != 1)
    {
        Serial.println(F("Wire buffer FULL at word address."));
        return false;
    }

    queued++;


    /*
     * Command bytes.
     */

    for (uint8_t i = 0; i < sizeof(packet); i++)
    {
        if (Wire.write(packet[i]) != 1)
        {
            Serial.print(F("Wire buffer FULL at command byte "));
            Serial.println(i);

            Serial.print(F("Actual queued bytes = "));
            Serial.println(queued);

            return false;
        }

        queued++;
    }


    /*
     * CRC bytes.
     */

    for (uint8_t i = 0; i < 2; i++)
    {
        if (Wire.write(crc[i]) != 1)
        {
            Serial.print(F("Wire buffer FULL at CRC byte "));
            Serial.println(i);

            Serial.print(F("Actual queued bytes = "));
            Serial.println(queued);

            return false;
        }

        queued++;
    }


    Serial.print(F("ACTUAL Wire bytes queued = "));
    Serial.println(queued);


    /*
     * We need exactly:
     *
     * 1 WordAddr
     * 37 packet
     * 2 CRC
     *
     * = 40
     */

    if (queued != 40)
    {
        Serial.println(F("STOP: Complete 40-byte packet not queued."));
        return false;
    }


    /*
     * ========================================================
     * TRANSMIT
     * ========================================================
     */

    uint8_t txResult =
        Wire.endTransmission();


    Serial.print(F("Wire.endTransmission = "));
    Serial.println(txResult);


    if (txResult != 0)
    {
        Serial.println(F("I2C transmission FAILED."));
        return false;
    }


    /*
     * ========================================================
     * WAIT FOR ATECC RESPONSE
     * ========================================================
     */

    Serial.println(F("Waiting for ATECC Write response..."));


    unsigned long start =
        millis();


    while ((millis() - start) < 500)
    {
        uint8_t received =
            Wire.requestFrom(
                DEVICE_ADDR,
                (uint8_t)4
            );


        if (received == 4)
        {
            uint8_t response[4];


            for (uint8_t i = 0; i < 4; i++)
            {
                response[i] =
                    Wire.read();
            }


            Serial.print(F("ATECC response: "));

            printBuffer(
                response,
                4
            );


            /*
             * Verify CRC.
             */

            if (!checkCRC(response, 4))
            {
                Serial.println(F("Response CRC FAILED."));
                return false;
            }


            if (response[0] != 0x04)
            {
                Serial.println(F("Unexpected response count."));
                return false;
            }


            /*
             * 0x00 = success.
             */

            if (response[1] == 0x00)
            {
                Serial.println(F("AES KEY WRITE SUCCESS."));
                return true;
            }


            Serial.print(F("ATECC Write status = 0x"));
            printHex(response[1]);
            Serial.println();

            return false;
        }


        while (Wire.available())
        {
            Wire.read();
        }


        delay(2);
    }


    /*
     * ========================================================
     * FAILURE DIAGNOSTIC
     * ========================================================
     */

    Serial.println(F("ERROR: No Write response after 500 ms."));


    /*
     * See if the ATECC can still be awakened.
     */

    Serial.println(F("Testing ATECC communication after failure..."));


    delay(5);


    if (wakeChip())
    {
        Serial.println(F("ATECC can still be awakened."));
        Serial.println(F("Likely malformed/truncated Write command."));
    }
    else
    {
        Serial.println(F("ATECC could not be awakened."));
        Serial.println(F("Check I2C bus / power / Wire implementation."));
    }


    Serial.println(F("DATA ZONE MUST REMAIN UNLOCKED."));

    return false;
}


/* ============================================================
 * AES BLOCK
 * ============================================================ */

bool aesBlock(
    bool decrypt,
    const uint8_t input[16],
    uint8_t output[16])
{

    if (!prepareChip())
        return false;

    uint8_t packet[21];


    packet[0] =
        0x17;

    packet[1] =
        OPCODE_AES;


    packet[2] =
        decrypt
        ? 0x01
        : 0x00;


    packet[3] =
        AES_SLOT;

    packet[4] =
        0x00;


    for (uint8_t i = 0;
         i < 16;
         i++)
    {
        packet[5 + i] =
            input[i];
    }


    uint8_t crc[2];


    calculateCRC(
        sizeof(packet),
        packet,
        crc
    );


    Serial.println(
        decrypt
        ? F("AES DECRYPT...")
        : F("AES ENCRYPT...")
    );


    Wire.beginTransmission(
        DEVICE_ADDR
    );


    Wire.write(
        WORD_ADDR_COMMAND
    );


    Wire.write(
        packet,
        sizeof(packet)
    );


    Wire.write(
        crc,
        2
    );


    uint8_t result =
        Wire.endTransmission();


    if (result != 0)
    {
        Serial.print(
            F("AES I2C error = ")
        );

        Serial.println(
            result
        );

        return false;
    }


    delay(15);


    uint8_t received =
        Wire.requestFrom(
            DEVICE_ADDR,
            (uint8_t)19
        );


    if (received < 4)
    {
        Serial.print(
            F("AES response too short: ")
        );

        Serial.println(
            received
        );

        return false;
    }


    uint8_t response[19];

    uint8_t count = 0;


    while (
        Wire.available() &&
        count < sizeof(response)
    )
    {
        response[count++] =
            Wire.read();
    }


    /*
     * 4-byte error/status response
     */

    if (
        count == 4 &&
        response[0] == 0x04
    )
    {
        Serial.print(
            F("AES status response: ")
        );


        printBuffer(
            response,
            4
        );


        if (!checkCRC(
                response,
                4))
        {
            Serial.println(
                F("AES status CRC FAILED")
            );

            return false;
        }


        Serial.print(
            F("AES returned status 0x")
        );

        printHex(
            response[1]
        );

        Serial.println();


        return false;
    }


    /*
     * Successful AES response
     */

    if (
        count != 19 ||
        response[0] != 0x13
    )
    {
        Serial.print(
            F("Unexpected AES response length = ")
        );

        Serial.println(
            count
        );

        return false;
    }


    if (!checkCRC(
            response,
            19))
    {
        Serial.println(
            F("AES response CRC FAILED")
        );

        return false;
    }


    for (uint8_t i = 0;
         i < 16;
         i++)
    {
        output[i] =
            response[i + 1];
    }


    return true;
}


/* ============================================================
 * AES TEST
 * ============================================================ */

bool runAESTest()
{
    uint8_t ciphertext[16];

    uint8_t recovered[16];


    Serial.println();

    Serial.println(
        F("==============================")
    );

    Serial.println(
        F("AES-128 HARDWARE TEST")
    );

    Serial.println(
        F("==============================")
    );


    Serial.print(
        F("Plaintext : ")
    );


    printBuffer(
        testPlaintext,
        16
    );


    if (!aesBlock(
            false,
            testPlaintext,
            ciphertext))
    {
        Serial.println(
            F("AES ENCRYPT FAILED")
        );

        return false;
    }


    Serial.print(
        F("Ciphertext: ")
    );


    printBuffer(
        ciphertext,
        16
    );


    if (!aesBlock(
            true,
            ciphertext,
            recovered))
    {
        Serial.println(
            F("AES DECRYPT FAILED")
        );

        return false;
    }


    Serial.print(
        F("Recovered : ")
    );


    printBuffer(
        recovered,
        16
    );


    for (uint8_t i = 0;
         i < 16;
         i++)
    {
        if (
            recovered[i] !=
            testPlaintext[i]
        )
        {
            Serial.println();

            Serial.println(
                F("*** AES TEST FAILED ***")
            );

            return false;
        }
    }


    Serial.println();

    Serial.println(
        F("==============================")
    );

    Serial.println(
        F("AES TEST SUCCESS")
    );

    Serial.println(
        F("Same Slot 8 key encrypted")
    );

    Serial.println(
        F("and decrypted successfully.")
    );

    Serial.println(
        F("==============================")
    );


    return true;
}


/* ============================================================
 * IDLE
 * ============================================================ */

void idleChip()
{
    Wire.beginTransmission(
        DEVICE_ADDR
    );


    Wire.write(
        WORD_ADDR_IDLE
    );


    Wire.endTransmission();
}


/* ============================================================
 * SETUP
 * ============================================================ */

void setup()
{
    Serial.begin(
        9600
    );


    Wire.begin();


    Wire.setClock(
        100000
    );


    delay(
        1500
    );


    Serial.println();

    Serial.println(
        F("================================")
    );

    Serial.println(
        F("ATECC608B AES CONFIG + PROVISION")
    );

    Serial.println(
        F("================================")
    );


    /*
     * ========================================================
     * WAKE
     * ========================================================
     */


    if (!wakeChip())
    {
        Serial.println(
            F("STOP: Wake failed.")
        );

        return;
    }


    /*
     * ========================================================
     * INITIAL CONFIG READ
     * ========================================================
     */


    if (!readConfig())
    {
        Serial.println(
            F("STOP: Initial configuration read failed.")
        );

        return;
    }


    printConfiguration();


    uint8_t lockConfig =
        configZone[87];

    uint8_t lockValue =
        configZone[86];


    /*
     * ========================================================
     * CONFIGURATION ZONE STILL OPEN
     *
     * Modify required bytes BEFORE locking.
     * ========================================================
     */


    if (lockConfig == 0x55)
    {
        if (lockValue != 0x55)
        {
            Serial.println(
                F("STOP: Config unlocked but Data locked.")
            );

            return;
        }


        Serial.println(
            F("Configuration zone is writable.")
        );


        if (!configureRequiredBytes())
        {
            Serial.println();

            Serial.println(
                F("STOP: Configuration update FAILED.")
            );

            Serial.println(
                F("NOTHING WILL BE LOCKED.")
            );

            return;
        }


        printConfiguration();


        if (!verifyStaticConfiguration())
        {
            Serial.println();

            Serial.println(
                F("STOP: Configuration verification FAILED.")
            );

            Serial.println(
                F("DO NOT LOCK CHIP.")
            );

            return;
        }


        /*
         * Lock Configuration only after successful verification.
         */


        if (!lockConfigZone())
        {
            Serial.println(
                F("STOP: Config lock failed.")
            );

            return;
        }


        /*
         * Verify lock transition.
         */


        if (!readConfig())
        {
            Serial.println(
                F("STOP: Cannot verify Config lock.")
            );

            return;
        }


        if (
            configZone[87] != 0x00 ||
            configZone[86] != 0x55
        )
        {
            Serial.println(
                F("STOP: Unexpected state after Config lock.")
            );

            printConfiguration();

            return;
        }


        Serial.println();

        Serial.println(
            F("CONFIG ZONE LOCKED SUCCESSFULLY.")
        );


        lockConfig =
            0x00;

        lockValue =
            0x55;
    }


    /*
     * ========================================================
     * CONFIG ALREADY LOCKED
     *
     * Configuration cannot be changed anymore.
     *
     * It MUST already contain the correct values.
     * ========================================================
     */


    else if (lockConfig == 0x00)
    {
        Serial.println(
            F("Configuration already locked.")
        );


        if (!verifyStaticConfiguration())
        {
            Serial.println();

            Serial.println(
                F("STOP: Locked configuration is incorrect.")
            );

            Serial.println(
                F("It cannot be repaired.")
            );

            return;
        }
    }


    else
    {
        Serial.print(
            F("STOP: Unknown LockConfig value 0x")
        );

        printHex(
            lockConfig
        );

        Serial.println();

        return;
    }


    /*
     * ========================================================
     * STATE B
     *
     * CONFIG LOCKED
     * DATA UNLOCKED
     *
     * Write key and lock Data.
     * ========================================================
     */


    if (
        lockConfig == 0x00 &&
        lockValue  == 0x55
    )
    {
        Serial.println();

        Serial.println(
            F("STATE B")
        );

        Serial.println(
            F("Config = LOCKED")
        );

        Serial.println(
            F("Data   = UNLOCKED")
        );


        /*
         * Write key.
         */


        if (!writeAESKey())
        {
            Serial.println();

            Serial.println(
                F("STOP: AES key write FAILED.")
            );

            Serial.println(
                F("DATA REMAINS UNLOCKED.")
            );

            Serial.println(
                F("DO NOT LOCK DATA.")
            );

            return;
        }


        /*
         * IMPORTANT:
         *
         * The ATECC accepted the key write.
         *
         * Secret Slot 8 cannot simply be read back in clear text,
         * so we rely on successful ATECC command status before
         * proceeding.
         */


        if (!lockDataZone())
        {
            Serial.println();

            Serial.println(
                F("STOP: Data lock failed.")
            );

            return;
        }


        /*
         * Verify Data lock.
         */


        if (!readConfig())
        {
            Serial.println(
                F("STOP: Cannot verify Data lock.")
            );

            return;
        }


        if (
            configZone[87] != 0x00 ||
            configZone[86] != 0x00
        )
        {
            Serial.println(
                F("STOP: Unexpected final lock state.")
            );

            printConfiguration();

            return;
        }


        Serial.println();

        Serial.println(
            F("DATA ZONE LOCKED SUCCESSFULLY.")
        );


        lockConfig =
            0x00;

        lockValue =
            0x00;
    }


    /*
     * ========================================================
     * STATE C
     *
     * FULLY PROVISIONED
     *
     * No writes.
     * No locking.
     *
     * AES test only.
     * ========================================================
     */


    if (
        lockConfig == 0x00 &&
        lockValue  == 0x00
    )
    {
        Serial.println();

        Serial.println(
            F("STATE C")
        );

        Serial.println(
            F("Config = LOCKED")
        );

        Serial.println(
            F("Data   = LOCKED")
        );

        Serial.println(
            F("Provisioning complete.")
        );


        if (!runAESTest())
        {
            Serial.println();

            Serial.println(
                F("AES TEST FAILED.")
            );

            return;
        }


        idleChip();


        return;
    }


    /*
     * Anything else is unexpected.
     */


    Serial.println();

    Serial.println(
        F("STOP: Unexpected device state.")
    );


    printConfiguration();
}


/* ============================================================
 * LOOP
 * ============================================================ */

void loop()
{
}