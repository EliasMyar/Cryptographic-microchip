#include <Wire.h>

#define DEVICE_ADDR 0x60
#define OPCODE_AES    0x51
#define AES_SLOT 8
#define WORD_ADDR_COMMAND  0x03

const uint8_t testPlaintext[] =
{
    'H','e','l','l','o',' ','W','o','r','l','d','!' 
};

// Calculates the size of the plaintext
const size_t plaintextSize = sizeof(testPlaintext) / sizeof(testPlaintext[0]);

const uint8_t output[plaintextSize];



/* ============================================================
 * SLEEP
 * ============================================================ */

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

void printText(const uint8_t *data, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++)
    {
        Serial.write(data[i]);
    }

    Serial.println();
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
    uint8_t ciphertext[plaintextSize];

    uint8_t recovered[plaintextSize];


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
        F("Plaintext (HEX) : ")
    );

    printBuffer(
        testPlaintext,
        plaintextSize
    );

    Serial.print(
        F("Plaintext (text) : ")
    );

    printText(testPlaintext,
    plaintextSize);

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
        F("Ciphertext (HEX): ")
    );


    printBuffer(
        ciphertext,
        plaintextSize
    );

      Serial.print(
        F("Ciphertext (text) : ")
    );
    
    printText(ciphertext,
    plaintextSize);


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
        F("Recovered (HEX): ")
    );


    printBuffer(
        recovered,
        plaintextSize
    );

      Serial.print(
        F("Recovered (text) : ")
    );
    
    printText(recovered,
    plaintextSize);


    for (uint8_t i = 0;
         i < plaintextSize;
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


void setup()
{
    Serial.begin(9600);

    Wire.begin();
    delay(100);

    Serial.println(F("WIRE STARTED"));

    Serial.println();

    bool result = runAESTest();

    Serial.print(F("Result: "));

    if (result)
    {
        Serial.println(F("SUCCESS"));
    }
    else
    {
        Serial.println(F("FAILED"));
    }
}

void loop() {
}
