#include <Wire.h>

#define DEVICE_ADDR 0x60 // ATECC608B I2C fixed Address
#define OPCODE_AES    0x51 // AES encrypt/decrypt opcode
#define AES_SLOT 8 // AES key slot (slot locked with the key on config)
#define WORD_ADDR_COMMAND  0x03 

#define AES_BLOCK_SIZE 16
#define MAX_PLAINTEXT_LEN 256 // Maximum text size to be encrypted for performance purposes

#define MAX_PHRASE_LEN (MAX_PLAINTEXT_LEN - AES_BLOCK_SIZE) // 112 characters, leaving 16 bytes for padding 

void sleepChip(){

    Wire.beginTransmission(DEVICE_ADDR);
    Wire.write(0x01); // Sleep
    Wire.endTransmission();

    delay(2); // 2 ms

}

void calculateCRC(uint8_t length, const uint8_t *data, uint8_t *crcOut){

    uint16_t crcRegister = 0; // 16-bit register where the CRC will be constructed

    const uint16_t polynom = 0x8005; // CRC generating polynomial (x^(16) + x^(15) + x^(2) + 1)

    // It iterates through the data byte by byte
    for (uint8_t counter = 0; counter < length; counter++){
        
        // It goes from the least to the most significant bit (LSB -> MSB)
        for (uint8_t shiftReg = 0x01; shiftReg != 0; shiftReg <<= 1){

            uint8_t dataBit = (data[counter] & shiftReg) ? 1 : 0; // Extrac the current bit using shiftReg as a mask
            uint8_t crcBit = (uint8_t)(crcRegister >> 15); // Extract the 15 bit of the crcRegister

            crcRegister <<= 1; // Shifts the entire register one position to the left (simulates the "advance" of a bit in the hardware shift register).

            if (dataBit != crcBit){

                // Whenever the bit extracted from the data is different from the bit extracted from the register, a polynomial division is applied to the register.
                crcRegister ^= polynom;            
        
            }
        
        }
    
    }

    crcOut[0] = (uint8_t)(crcRegister & 0xFF); // Least significant byte on the first position (little-endian)
    crcOut[1] = (uint8_t)(crcRegister >> 8); // Most significant byte on the last position

}

bool checkCRC(const uint8_t *response, uint8_t length){

    // Each ATECC response has at least 2 CRC bytes at the end and at least one extra byte of data before the CRC (example: the byte count)
    if (length < 3){

        return false;
    
    }

    uint8_t crc[2];
    calculateCRC(length - 2, response, crc); // Calculate the CRC of the data before the received CRC

    return response[length - 2] == crc[0] && response[length - 1] == crc[1]; // Compare the least significant bit (LSB) of the received CRC with the calculated one and repeat the process for the most significant bit (MSB) of both

}

void printHex(uint8_t value){

    // Arduino do not add the 0 when the Hex number is 0X
    if (value < 0x10){

        Serial.print('0');
    
    }

    Serial.print(value, HEX); // Print the Hex version of each byte

}

void printBuffer(const uint8_t *data, uint8_t length){

    // Iterates through each byte of the data
    for (uint8_t i = 0; i < length; i++){

        printHex(data[i]);

        if (i != length - 1){

            Serial.print(' ');
        
        }
    
    }

    Serial.println(); // Print the hole buffer in Hex

}

void printText(const uint8_t *data, uint8_t length){

    for (uint8_t i = 0; i < length; i++){

        Serial.write(data[i]); // Send the information exactly as it appears on the port
    
    }

    Serial.println(); // Print the buffer data as text (ASCII)

}

bool wakeChip(){

    // The combination of beginTransmission(0x00) and write(0x00) creates a "wake pulse"
    Wire.beginTransmission(0x00);
    Wire.write(0x00);
    Wire.endTransmission();

    delayMicroseconds(1500); // 1.5 ms

    uint8_t received = Wire.requestFrom(DEVICE_ADDR, (uint8_t)4);

    if (received != 4){

        Serial.print(F("Wake response length = "));
        Serial.println(received); // If the wake response size is not 4, activation failed.

        return false;
    
    }

    uint8_t response[4];

    for (uint8_t i = 0; i < 4; i++){

        response[i] = Wire.read();
    
    }

    Serial.print(F("Wake: "));
    printBuffer(response, 4);

    if (!checkCRC(response, 4)){

        // Checks the last two bytes from the CRC message came from the wake response (04 11 <CRC> <CRC>). If the CRC is not the expected, activation failed
        Serial.println(F("Wake CRC FAILED"));

        return false;
    
    }

    if (response[0] != 0x04 || response[1] != 0x11){

        // Checks if the first two byteas are not 0x04 and 0x11. Any other byte in the first two positions means activation failed
        Serial.println(F("Invalid wake response"));

        return false;
    
    }

    return true;

}

bool prepareChip(){

    // Ensures that the chip is always starting up from the sleep
    sleepChip();
    delay(2);
    Serial.println(F("Waking chip for command..."));

    if (!wakeChip()){

        Serial.println(F("ERROR: Cannot wake ATECC608B")); // Detects problems on the wake response
        
        return false;
    
    }

    return true; // The chip is ready for encoding/decoding

}

bool aesBlock(bool decrypt, const uint8_t input[16], uint8_t output[16]){

    if (!prepareChip()){

        return false;

    }

    uint8_t packet[21]; // Initialize the control packet, sending 21 bytes + CRC

    packet[0] = 0x17; // Count = dec(0x17) = 23 bytes = packet + 2 bytes(CRC)
    packet[1] = OPCODE_AES; // Opcode (0x51)
    packet[2] = decrypt ? 0x01 : 0x00; // Mode: 0x01 = Decrypt and 0x00 = Encrypt
    packet[3] = AES_SLOT; // Low byte key slot = 8 (AES key)
    packet[4] = 0x00; // High byte key slot = 0

    for (uint8_t i = 0; i < 16; i++){

        packet[5 + i] = input[i]; // 16 bytes de dados
    
    }

    uint8_t crc[2]; // 2 bytes of CRC

    calculateCRC(sizeof(packet), packet, crc);

    Serial.println(decrypt ? F("AES DECRYPT...") : F("AES ENCRYPT..."));

    Wire.beginTransmission(DEVICE_ADDR);
    Wire.write(WORD_ADDR_COMMAND); // Communicates and says, "This is a command"
    Wire.write(packet, sizeof(packet));
    Wire.write(crc, 2);

    uint8_t result = Wire.endTransmission();

    if (result != 0){

        Serial.print(F("AES I2C error = ")); // Any error in communication is interrupted and displayed on the serial monitor.
        Serial.println(result);

        return false;
    
    }

    delay(15);

    uint8_t received = Wire.requestFrom(DEVICE_ADDR, (uint8_t)19);

    if (received < 4){

        Serial.print(F("AES response too short: ")); // Abosrts if the response is shorter than 4 bytes while waiting for a maximum of 19 bytes response
        Serial.println(received);

        return false;
    
    }

    uint8_t response[19];
    uint8_t count = 0;

    while (Wire.available() && count < sizeof(response)){

        response[count++] = Wire.read(); // Read the 4 or 19 bytes of response and add the number of real bytes read in count
    
    }

    if (count == 4 && response[0] == 0x04){

        Serial.print(F("AES status response: ")); // If the response has 4 bytes in total it means that it is a status/error response
        printBuffer(response, 4);

        if (!checkCRC(response, 4)){

            Serial.println(F("AES status CRC FAILED")); // If it is a CRC error

            return false;

        }

        Serial.print(F("AES returned status 0x")); // Any other error will be shown as it's hex code
        printHex(response[1]);
        Serial.println();

        return false;
    
    }

    if (count != 19 || response[0] != 0x13){

        Serial.print(F("Unexpected AES response length = ")); // For each different response with a count other than 0x13 (19 bytes), it is either a corruption or an unexpected response
        Serial.println(count);

        return false;
    
    }

    if (!checkCRC(response, 19)){

        Serial.println(F("AES response CRC FAILED")); // Validates the CRC of the received data 

        return false;
    
    }

    for (uint8_t i = 0; i < 16; i++){

        output[i] = response[i + 1]; // Take the data from the response, after the count byte
    
    }

    return true;

}

size_t readPhraseFromSerial(uint8_t *buffer, size_t maxLen){

    Serial.println();
    Serial.print(F("Escreva uma frase (max "));
    Serial.print(maxLen);
    Serial.println(F(" caracteres) e prima ENTER:"));

    size_t length = 0;

    while (true){

        if (Serial.available()){

            char c = Serial.read();

            if (c == '\n' || c == '\r'){ // It detects the "ENTER" button so that the Arduino receives it as a command

                if (length > 0){

                    break; // It does not react to the "ENTER" key being pressed without any bytes having been typed
                }

                continue;
            
            }

            if (length < maxLen){

                buffer[length++] = (uint8_t)c; // If the character is not a "ENTER" it is stoered inside the lenght variable
            
            }
        
        }
    
    }

    delay(2);

    while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')){

        Serial.read(); // Clean the data that is waiting on the buffer
    
    }

    return length; // Return the actual size of the read data

}

size_t padPKCS7(uint8_t *buffer, size_t length){

    uint8_t padValue = AES_BLOCK_SIZE - (length % AES_BLOCK_SIZE); // Detects the amount of bytes missing to have a 16 bytes block
    size_t paddedLength = length + padValue; // Add at least a block of 16 padding bytes

    for (size_t i = length; i < paddedLength; i++){

        buffer[i] = padValue;
    
    }

    return paddedLength;

}

size_t unpadPKCS7(const uint8_t *buffer, size_t length){

    if (length == 0){

        return 0; // No data no padding
    
    }

    uint8_t padValue = buffer[length - 1]; // Read the last byte to know how much padding were added

    if (padValue == 0 || padValue > AES_BLOCK_SIZE || padValue > length){

        return length; // If the amount of padding is 0 or bigger than the block size (16 bytes) or bigger than the buffer itself
    
    }

    return length - padValue;

}

bool runAESTest(uint8_t *phrase, size_t phraseLength){

    uint8_t ciphertext[MAX_PLAINTEXT_LEN]; // Alocates memory for the encrypted bytes
    uint8_t recovered[MAX_PLAINTEXT_LEN]; // Alocates memort for the decrypted bytes
    size_t paddedLength = padPKCS7(phrase, phraseLength); // Calculate and add the padding
    size_t numBlocks = paddedLength / AES_BLOCK_SIZE; // Calculate the number of AES blocks the phrase has

    Serial.println();
    Serial.println(F("=============================="));
    Serial.println(F("AES-128 HARDWARE TEST"));
    Serial.println(F("=============================="));
    Serial.print(F("Frase (texto)        : "));
    printText(phrase, phraseLength); // Print the text in ASCII
    Serial.print(F("Frase + padding (HEX): "));
    printBuffer(phrase, paddedLength); // Print the text in hex
    Serial.print(F("Blocos de 16 bytes a processar: "));
    Serial.println((unsigned int)numBlocks); // The number of AES blocks

    for (size_t b = 0; b < numBlocks; b++){

        if (!aesBlock(false, & phrase[b * AES_BLOCK_SIZE], & ciphertext[b * AES_BLOCK_SIZE])){ // Encrypts the data for each block of 16 bytes with both pointers (&phrase[...] and &ciphertext[...]) pointing directly to the location calculated by b*AES_BLOCK_SIZE (0 = first block of 16 bytes, 16 = second block of 16 bytes...)

            Serial.println(F("AES ENCRYPT FAILED")); // If aesBlock(Encryption...) is aborted, the encryption will also be aborted

            return false;
        
        }
    
    }

    Serial.print(F("Ciphertext (HEX): "));
    printBuffer(ciphertext, paddedLength); // Print the encrypted data in hex

    for (size_t b = 0; b < numBlocks; b++){

        if (!aesBlock(true, & ciphertext[b * AES_BLOCK_SIZE], & recovered[b * AES_BLOCK_SIZE])){// Decrypts the data for each block of 16 bytes with both pointers (&phrase[...] and &ciphertext[...]) pointing directly to the location calculated by b*AES_BLOCK_SIZE (0 = first block of 16 bytes, 16 = second block of 16 bytes...)

            Serial.println(F("AES DECRYPT FAILED")); // If aesBlock(Decryption...) is aborted, the encryption will also be aborted

            return false;
        
        }
    
    }

    Serial.print(F("Recovered (HEX): "));
    printBuffer(recovered, paddedLength); // Print the decrypted data in hex

    size_t recoveredLength = unpadPKCS7(recovered, paddedLength); // Clear the padding

    Serial.print(F("Recovered (texto)    : "));
    printText(recovered, recoveredLength); // Read the data as text (ASCII)

    for (size_t i = 0; i < paddedLength; i++){

        if (recovered[i] != phrase[i]){

            Serial.println();
            Serial.println(F("*** AES TEST FAILED ***")); // If there is any difference between the decrypted text and the message sent the test is failed

            return false;
        
        }
    
    }

    Serial.println();
    Serial.println(F("=============================="));
    Serial.println(F("AES TEST SUCCESS"));
    Serial.print(F("Slot 8: "));
    Serial.print((unsigned int)numBlocks);
    Serial.println(F(" bloco(s) cifrados e decifrados com sucesso."));
    Serial.println(F("=============================="));

    return true;

}

void setup(){

    Serial.begin(9600);

    Wire.begin();
    delay(100);

    Serial.println(F("WIRE STARTED"));

}

void loop(){

    uint8_t phrase[MAX_PLAINTEXT_LEN];
    size_t phraseLength = readPhraseFromSerial(phrase, MAX_PHRASE_LEN); // Read the serial and waits for a "ENTER"
    bool result = runAESTest(phrase, phraseLength); // Do a complete AES test with encryption and decryption
    Serial.print(F("Result: "));

    if (result){
        
        Serial.println(F("SUCCESS"));
    
    }
    
    else{

        Serial.println(F("FAILED"));
    
    }

}
