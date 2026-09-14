# ATECC608B AES-128 Provisioning & File Encryption Toolkit

Tools for provisioning a Microchip **ATECC608B** secure element (AES-128 key
in Slot 8) over I2C from an **Arduino Uno R3**, and for using that key to
encrypt/decrypt arbitrary files from a PC. The five files here aren't
independent alternatives — they represent different **stages** of the same
workflow, from one-time chip setup to everyday file encryption.

> **Locking a chip's Config/Data zones is permanent and cannot be undone.**
> Read `Micrichip_sketch.ino` fully and test on a spare chip before running
> it against hardware you care about.

## Repository structure

| File | Role |
|---|---|
| `Micrichip_sketch.ino` | **One-time provisioning.** Locks the Config Zone, writes the AES key into Slot 8, locks the Data Zone. |
| `Encrypt_decrypt.ino` | **Sanity check.** Minimal sketch that just encrypts/decrypts one fixed built-in test string using the already-provisioned key. |
| `ATECC608B_test.ino` | **Interactive manual test.** Type any phrase into the Serial Monitor; the Arduino pads, encrypts, decrypts, and verifies it — all on-device. |
| `ATECC608B_test2.ino` | **File-encryption firmware.** Pairs with `enc_dec_file.py` — handles only the AES calls over I2C; all padding/chunking is done on the PC side. |
| `enc_dec_file.py` | **File-encryption host script.** Reads a file of any size, chunks and pads it, sends each chunk to `ATECC608B_test2.ino` over serial, and reassembles the result. |

## The workflow, stage by stage

### Stage 1 — Provision the chip (`Micrichip_sketch.ino`)

This is the only sketch that touches the Config/Data zone lock state. It's
written as a **state machine** that reads the chip's current `LockConfig`
and `LockValue` bytes on every boot and resumes wherever it left off, so
it's safe to re-run after a power loss or a failed step:

- **State A — nothing locked yet:** writes the required Config Zone bytes,
  verifies them, then locks the Config Zone.
- **State B — Config locked, Data unlocked:** writes the AES-128 key into
  Slot 8, then locks the Data Zone.
- **State C — fully provisioned:** runs a self-test (encrypt → decrypt →
  compare) using the key already in Slot 8 and stops. No further writes or
  locks happen in this state.

At every step it verifies the result before proceeding to the next
irreversible action (it will refuse to lock the Data Zone if the key write
didn't return success, for example). Run this **once** per chip.

### Stage 2 — Confirm the key works

Two options, depending on what you need:

- **`Encrypt_decrypt.ino`** — the quickest check. No user interaction: it
  just encrypts and decrypts one hardcoded 16-byte string
  (`"ATECC608B-AES-01"`) with the Slot 8 key and prints whether it matches.
  Good as a first smoke test right after provisioning.
- **`ATECC608B_test.ino`** — a more flexible manual test. Prompts you for a
  phrase over the Serial Monitor (up to ~240 characters), applies PKCS#7
  padding **on the Arduino itself**, encrypts, decrypts, strips the padding,
  and prints all three forms (plaintext, ciphertext, recovered text) so you
  can eyeball a full round trip with your own input. The ~256-byte ceiling
  here is a hard limit of the Uno R3's 2 KB of SRAM — everything happens
  in on-board buffers, so it can't scale to real files.

### Stage 3 — Encrypt/decrypt real files (`ATECC608B_test2.ino` + `enc_dec_file.py`)

This pair splits the work so file size is no longer limited by the Arduino's
RAM: the PC does all the buffering, and the Arduino only ever sees one small
16-byte-aligned chunk at a time.

**Division of responsibilities:**

| Task | Handled by |
|---|---|
| Reading the input file | `enc_dec_file.py` |
| Splitting it into `MAX_PHRASE_LEN`-sized chunks | `enc_dec_file.py` |
| PKCS#7 padding (encrypt) / unpadding (decrypt) | `enc_dec_file.py` |
| Converting each chunk to/from hex for serial transport | `enc_dec_file.py` |
| Serial protocol (`ENC:<hex>` / `DEC:<hex>` framing, timeouts, boot-message detection, error lines) | `enc_dec_file.py` (sender) + `ATECC608B_test2.ino` (receiver) |
| Talking to the ATECC608B over I2C for each 16-byte block | `ATECC608B_test2.ino` |
| Reassembling the output file | `enc_dec_file.py` |

Unlike `ATECC608B_test.ino`, the `.ino` side here does **no padding of its
own** — it trusts that every chunk arriving from Python is already a
multiple of 16 bytes, and just runs each 16-byte block through `aesBlock()`
in sequence. All the "is this a valid multiple of the block size / has the
Arduino unexpectedly rebooted / did we get a valid hex response" bookkeeping
lives in `enc_dec_file.py`.

### Usage

1. Flash `ATECC608B_test2.ino` to the Arduino (after Stage 1 provisioning is
   already done).
2. From the PC:

```bash
pip install pyserial

# Encrypt (default)
python enc_dec_file.py plaintext.txt encrypted.bin

# Decrypt
python enc_dec_file.py encrypted.bin recovered.txt --decrypt
```

Update `SERIAL_PORT` at the top of `enc_dec_file.py` to match your board
(`COM11` by default on Windows; something like `/dev/ttyACM0` or
`/dev/ttyUSB0` on Linux/macOS).

## Requirements

- Arduino Uno R3 (or compatible AVR board with the same SRAM constraints)
- ATECC608B secure element wired over I2C (SDA/SCL + pull-ups)
- Arduino IDE or PlatformIO to flash the `.ino` sketches
- Python 3 with `pyserial` for `enc_dec_file.py`

## Notes and caveats

- All four sketches currently use `DEVICE_ADDR 0x60`. If your chip has been
  reconfigured to a different I2C address at some point, update this
  `#define` consistently across whichever sketch you're flashing.
- The AES mode in use throughout is **ECB** — each 16-byte block is
  encrypted independently, with no IV or chaining. This is a limitation of
  the ATECC608B's native `AES` command in this configuration, not a bug in
  these scripts; be aware that identical plaintext blocks always produce
  identical ciphertext blocks.
- The AES key baked into `Micrichip_sketch.ino` is explicitly commented as
  a **test/development key**. Generate and use your own key (ideally
  written directly by the host during provisioning rather than hardcoded in
  source) before relying on this for anything beyond bench testing.
- `enc_dec_file.py` detects an unexpected Arduino reboot mid-transfer (by
  watching for the `"WIRE STARTED"` boot message reappearing) and aborts
  with a clear error rather than silently producing a corrupt output file.
