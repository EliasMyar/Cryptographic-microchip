#include <Wire.h>

#define DEVICE_ADDR 0x60 // ATECC608B I2C fixed Address
#define OPCODE_AES    0x51 // AES encrypt/decrypt opcode
#define AES_SLOT 8 // AES key slot (slot locked with the key on config)
#define WORD_ADDR_COMMAND  0x03 

#define AES_BLOCK_SIZE 16
#define MAX_PLAINTEXT_LEN 256 // Maximum text size to be encrypted because of Arduino SRAM limitations

#define MAX_PHRASE_LEN (MAX_PLAINTEXT_LEN - AES_BLOCK_SIZE)

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

            crcRegister <<= 1; // Shifts the entire register one position to the left (simulates the "advance" of a bit in the hardware shift register)

            if (dataBit != crcBit){

                // Whenever the bit extracted from the data is different from the bit extracted from the register, a polynomial division is applied to the register
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

bool wakeChip(){

    // The combination of beginTransmission(0x00) and write(0x00) creates a "wake pulse"
    Wire.beginTransmission(0x00);
    Wire.write(0x00);
    Wire.endTransmission();

    delayMicroseconds(1500); // 1.5 ms

    uint8_t received = Wire.requestFrom(DEVICE_ADDR, (uint8_t)4);

    if (received != 4){

        Serial.print(F("Wake response length = "));
        Serial.println(received); // If the wake response size is not 4, activation failed

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

    Wire.beginTransmission(DEVICE_ADDR);
    Wire.write(WORD_ADDR_COMMAND); // Communicates and says, "This is a command"
    Wire.write(packet, sizeof(packet));
    Wire.write(crc, 2);

    uint8_t result = Wire.endTransmission();

    if (result != 0){

        Serial.print(F("AES I2C error = ")); // Any error in communication is interrupted and displayed on the serial monitor
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

uint8_t HexToNibble(char c) { // Transform a single Hex into it's numerical value of 4 bits (nibble = half of a byte)

    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    
    return 0;

}

void setup(){

    Serial.begin(9600);

    Wire.begin();
    delay(100);

    Serial.println("");
    Serial.println(F("WIRE STARTED"));

}

#define LINE_BUFFER_LEN (MAX_PHRASE_LEN * 2 + 8) // Fixed buffer for the received line: prefix (4) + MAX_PHRASE_LEN hex bytes (2 chars/byte) + margin

size_t readLineFromSerial(char *buffer, size_t maxLen){

    size_t length = 0;

    while (true){

        if (Serial.available()){

            char c = Serial.read();

            if (c == '\n' || c == '\r'){

                if (length > 0){

                    break;

                }

                continue; // Ignore any empty "ENTER" at the beginning

            }

            if (length < maxLen - 1){

                buffer[length++] = c;

            }

        }

    }

    buffer[length] = '\0';

    while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')){

        Serial.read(); // Cleans "\r\n" that may have remained in the serial buffer

    }

    return length;

}

char lineBuffer[LINE_BUFFER_LEN];
uint8_t phrase[MAX_PLAINTEXT_LEN];
uint8_t output[MAX_PLAINTEXT_LEN];

void loop() {

    if (!Serial.available()){ 
        
        return; // Wait for the nest loop() call to check if the serial is available
    
    }
    
    size_t lineLen = readLineFromSerial(lineBuffer, LINE_BUFFER_LEN); // Reads the line into the global buffer (without heap)

    if (lineLen == 0) {
        
        return; // If the "ENTER" is pressed and nothig was typed the Arduino waits for the next loop() call with data
    
    } 

    bool decrypt = false;
    const char *hexStart = lineBuffer;
    size_t hexLen = lineLen;

    if (lineLen >= 4 && strncmp(lineBuffer, "ENC:", 4) == 0) { // Encryption

        decrypt = false;
        hexStart = lineBuffer + 4;
        hexLen = lineLen - 4;
    
    } else if (lineLen >= 4 && strncmp(lineBuffer, "DEC:", 4) == 0) { // Decryption
    
        decrypt = true;
        hexStart = lineBuffer + 4;
        hexLen = lineLen - 4;
    
    } // else: If there is no prefix it assumes encryption (hexStart/hexLen já apontam para o lineBuffer completo)

    size_t byteLen = hexLen / 2; // A pair of hex characters (nibbles) represents 1 byte of data

    if (byteLen == 0 || byteLen > MAX_PHRASE_LEN) {
    
        Serial.println("ERROR_LENGTH"); // Detects if there is no data or if it is larger than the maximum
    
        return;
    
    }

    for (size_t i = 0; i < byteLen; i++) {
    
        char high = hexStart[i * 2];
        char low  = hexStart[i * 2 + 1];
        phrase[i] = (HexToNibble(high) << 4) | HexToNibble(low); // Reconstructs the original byte from its two hex characters
    
    }

    if (byteLen % AES_BLOCK_SIZE != 0) {

        Serial.println(decrypt ? "ERROR_DECRYPT_LENGTH" : "ERROR_ENCRYPT_LENGTH"); // Detects if the data to be encrypted/decrypted is valid (multiple of 16 bytes)

        return;

    }

    if (!prepareChip()) { // Wake up the chip ONCE per request (not per 16-byte sub-block)

        Serial.println(decrypt ? "ERROR_DECRYPT" : "ERROR_ENCRYPT");

        return;

    }

    size_t paddedLength = byteLen;
    size_t numBlocks = paddedLength / AES_BLOCK_SIZE; // Check the number of blocks of 16 bytes is needed to read whole data

    for (size_t b = 0; b < numBlocks; b++) {

        if (!aesBlock(decrypt, &phrase[b * AES_BLOCK_SIZE], &output[b * AES_BLOCK_SIZE])) {

            Serial.println(decrypt ? "ERROR_DECRYPT" : "ERROR_ENCRYPT"); // Decrypts/encrypts the data and checks if there was any error in the process
                
            return;
            
        }
        
    }
        
    for (size_t i = 0; i < paddedLength; i++) {

        printHex(output[i]); // Send the decrypted/encrypted data
        
    }
        
    Serial.println(); // Terminates the line so the receiver knows the response ended

}