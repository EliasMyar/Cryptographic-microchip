import importlib
try:

    serial = importlib.import_module('serial')

except ModuleNotFoundError as err:

    raise RuntimeError('pyserial is required; install it with: python -m pip install pyserial') from err

import time
import sys
import argparse
import os

SERIAL_PORT = 'COM11'
BAUDRATE = 9600
TIMEOUT = 5

AES_BLOCK_SIZE = 16
MAX_PLAINTEXT_LEN = 256
MAX_PHRASE_LEN = MAX_PLAINTEXT_LEN - AES_BLOCK_SIZE

BOOT_MESSAGE = "WIRE STARTED"


def wakeSerial(port=SERIAL_PORT, baud=BAUDRATE, timeout=TIMEOUT):

    ser = serial.Serial(port, baud, timeout=timeout) # Open the communication between Arduino and Python sketch
    time.sleep(2) # Wait for the Arduino boot process to finish after resetting via DTR
    ser.reset_input_buffer() # Discard "WIRE STARTED" and any other boot junk

    return ser


def hex_l(line):

    if not line or len(line) % 2 != 0:

        return False # Checks if the line is a valid hexadecimal string; Otherwise, returns false

    return all(c in '0123456789abcdefABCDEF' for c in line)


DEBUG_LINES_PER_REQUEST = 2  # "Waking chip for command...", "Wake: 04 11 XX XX"
DEBUG_LINES_MARGIN = 10 # Allow some leeway to tolerate unexpected bugs without giving up too soon


def debug_l():

    return DEBUG_LINES_PER_REQUEST + DEBUG_LINES_MARGIN # Maximum number of debug lines tolerated before giving up


def readDebug(ser, m_debug_l):

    for _ in range(m_debug_l):

        line = ser.readline().decode('utf-8', errors='replace').strip() # Read a line on the serial

        if not line:

            raise RuntimeError('No response from Arduino (timeout)') # No data arrived within the timeout (port disconnected, Arduino stuck, etc.)

        if line == BOOT_MESSAGE:

            raise RuntimeError('Arduino restarted unexpectedly') # The board restarted (watchdog, brownout, DTR, etc.)

        if line.startswith('ERROR') or hex_l(line):

            return line # Returns the actual response, whether it's an error ("ERROR_...") or a valid result (hex)

        print(f'  [debug Arduino] {line}')

    raise RuntimeError('Too many debug lines without a valid response')


def sendBlock(ser, dataBytes, decrypt=False):

    if len(dataBytes) > MAX_PHRASE_LEN: # Checks if the data length exceeds the maximum allowed

        raise ValueError(f'Block too big: {len(dataBytes)} bytes (máx. {MAX_PHRASE_LEN})')

    if len(dataBytes) % AES_BLOCK_SIZE != 0: # Checks if the data length is a mutiple of the AES block size

        raise ValueError(f'Invalid block: {len(dataBytes)} bytes it is not a mutiple of {AES_BLOCK_SIZE} 'f'(the padding must be applied to the file before calling the sendBloc(...))')

    hexStr = dataBytes.hex() 
    prefix = "DEC:" if decrypt else "ENC:"
    fullLine = prefix + hexStr

    print(f'  [sent] prefix={prefix!r} len(hexStr)={len(hexStr)} len(line)={len(fullLine)}')

    ser.write((fullLine + '\n').encode('utf-8'))

    line = readDebug(ser, debug_l())

    if line.startswith('ERROR'):

        raise RuntimeError(f'Error in operation: {line}')

    if not hex_l(line):

        raise RuntimeError(f'Invalid response from Arduino: {line!r}')

    return bytes.fromhex(line)


def pkcs7_pad(data): # Apply PKCS7 padding to the given data block

    pad_len = AES_BLOCK_SIZE - (len(data) % AES_BLOCK_SIZE)

    return data + bytes([pad_len]) * pad_len


def pkcs7_unpad(data): # Remove PKCS7 padding from the given data block

    if not data:

        return data # If the data is empty, return it as is (no padding to remove)

    pad_len = data[-1]

    if pad_len < 1 or pad_len > AES_BLOCK_SIZE: # If the padding length is invalid (less than 1 or greater than the AES block size), return the data as is (no valid padding to remove)

        return data

    for i in range(1, pad_len + 1):

        if data[-i] != pad_len:

            return data

    return data[:-pad_len]


def process_f(in_f, out_f, port=SERIAL_PORT, baud=BAUDRATE, decrypt=False):

    if decrypt:

        f_size = os.path.getsize(in_f)

        if f_size % AES_BLOCK_SIZE != 0:

            raise ValueError(f'Input file with {f_size} bytes is not a multiple of ' f'{AES_BLOCK_SIZE} - it cannot be a valid PKCS7 ciphertext')

    ser = wakeSerial(port, baud)
    blockCount = 0 # Alocates a variable to count the number of blocks processed

    start_t = time.perf_counter() # Start the timer to measure the total processing time

    with ser, open(in_f, 'rb') as fin, open(out_f, 'wb') as fout: # Open the serial port and the input/output files in binary mode

        if decrypt:

            accumulated = bytearray() # Alocates a bytearray to accumulate the decrypted blocks

            while True:

                block = fin.read(MAX_PHRASE_LEN) # Read a block of data from the input file, up to the maximum phrase length

                if not block:

                    break # If the block is empty, break the loop (end of file)

                blockCount += 1
                print(f'Block {blockCount}: sending {len(block)} bytes...')
                resultBlock = sendBlock(ser, block, decrypt=True) # Send the block to the Arduino with "DEC:" prefix for decryption
                accumulated.extend(resultBlock)
                print(f'Block {blockCount}: received {len(resultBlock)} bytes')

            unpadded = pkcs7_unpad(bytes(accumulated)) # Remove the PKCS7 padding from the accumulated decrypted data
            fout.write(unpadded)

        else:

            plaintext = fin.read() # Read the entire input file as plaintext
            padded = pkcs7_pad(plaintext) # Apply PKCS7 padding to the plaintext to ensure its length is a multiple of the AES block size

            for i in range(0, len(padded), MAX_PHRASE_LEN):

                block = padded[i:i + MAX_PHRASE_LEN]

                blockCount += 1
                print(f'Block {blockCount}: sending {len(block)} bytes...')
                resultBlock = sendBlock(ser, block, decrypt=False) # Send the block to the Arduino with "ENC:" prefix for encryption
                fout.write(resultBlock)
                print(f'Block {blockCount}: received {len(resultBlock)} bytes')

    end_t = time.perf_counter()

    print()
    print('==============================')
    print('OPERATION COMPLETED')
    print(f'Mode: {"DECRYPTION" if decrypt else "ENCRYPTION"}')
    print(f'Total blocks processed: {blockCount}')
    print(f'Total processing time: {end_t - start_t:.2f} seconds')
    print('==============================')


if __name__ == '__main__':

    parser = argparse.ArgumentParser(description='Encrypts or decrypts a file using the ATECC608B via Arduino.')
    parser.add_argument('input', help='Input file')
    parser.add_argument('output', help='Output file')

    group = parser.add_mutually_exclusive_group()
    group.add_argument('-e', '--encrypt', action='store_true', help='Encrypt (default)')
    group.add_argument('-d', '--decrypt', action='store_true', help='Decrypt')

    args = parser.parse_args()

    mode = args.decrypt # Gets the mode of operation (encryption or decryption) based on the command-line arguments

    try:

        process_f(args.input, args.output, decrypt=mode)

    except (RuntimeError, ValueError, serial.SerialException) as err:

        print(f'ERROR: {err}')
        sys.exit(1) # Exit with a non-zero status code to indicate an error occurred