#include "pic_io.h"
#include "pic_devices.h"
#include "sp.h"

#include <c_types.h>
#include <mem.h>


static int _state;
static uint64_t _program_counter; 


// Flat address ranges for the various memory spaces.  Defaults to the values
// for the PIC16F628A.  "DEVICE" command updates to the correct values later.
static uint64_t programEnd			 = 0x07FF;
static uint64_t configStart  		 = 0x2000;
static uint64_t configEnd    		 = 0x2007;
static uint64_t dataStart    		 = 0x2100;
static uint64_t dataEnd      		 = 0x217F;
static uint64_t reservedStart		 = 0x0800;
static uint64_t reservedEnd  		 = 0x07FF;
static uint32_t configSave   		 = 0x0000;
static uint8_t progFlashType		= FLASH4;
static uint8_t dataFlashType		= EEPROM;


// Enter high voltage programming mode.
static ICACHE_FLASH_ATTR
void _enter_program_mode() {
    // Bail out if already in programming mode.
    if (_state != STATE_IDLE)
        return;
    // Lower MCLR, VDD, DATA, and CLOCK initially.  This will put the
    // PIC into the powered-off, reset state just in case.
    GPIO_SET(MCLR_NUM, MCLR_RESET);
    GPIO_SET(VDD_NUM, LOW);
    GPIO_SET(DATA_NUM, LOW);
    GPIO_SET(CLOCK_NUM, LOW);
    // Wait for the lines to settle.
    os_delay_us(DELAY_SETTLE);
    // Switch DATA and CLOCK into outputs.
    GPIO_OUTPUT(DATA_NUM);
    GPIO_OUTPUT(CLOCK_NUM);
    // Raise MCLR, then VDD.
    GPIO_SET(MCLR_NUM, MCLR_VPP);
    os_delay_us(DELAY_TPPDP);
    GPIO_SET(VDD_NUM, HIGH);
    os_delay_us(DELAY_THLD0);
    // Now in program mode, starting at the first word of program memory.
    _state = STATE_PROGRAM;
    _program_counter = 0;
}


// Exit programming mode and reset the device.
static ICACHE_FLASH_ATTR
void _exit_program_mode() {
    // Nothing to do if already out of programming mode.
    if (_state == STATE_IDLE)
        return;
    // Lower MCLR, VDD, DATA, and CLOCK.
    GPIO_SET(MCLR_NUM, MCLR_RESET);
    GPIO_SET(VDD_NUM, LOW);
    GPIO_SET(DATA_NUM, LOW);
    GPIO_SET(CLOCK_NUM, LOW);

    // Float the DATA and CLOCK pins.
    GPIO_INPUT(DATA_NUM);
    GPIO_INPUT(CLOCK_NUM);
    // Now in the idle state with the PIC powered off.
    _state = STATE_IDLE;
    _program_counter = 0;
}


// Send a command to the PIC.
static ICACHE_FLASH_ATTR
void _send_command(uint8_t cmd) {
	uint8_t bit;
    for (bit = 0; bit < 6; ++bit) {
        GPIO_SET(CLOCK_NUM, HIGH);
        if (cmd & 1)
            GPIO_SET(DATA_NUM, HIGH);
        else
            GPIO_SET(DATA_NUM, LOW);
        os_delay_us(DELAY_TSET1);
        GPIO_SET(CLOCK_NUM, LOW);
        os_delay_us(DELAY_THLD1);
        cmd >>= 1;
    }
}


// Send a command to the PIC that has no arguments.
static ICACHE_FLASH_ATTR
void _send_simple_command(uint8_t cmd) {
    _send_command(cmd);
    os_delay_us(DELAY_TDLY2);
}


// Send a command to the PIC that writes a data argument.
static ICACHE_FLASH_ATTR
void _send_write_command(uint8_t cmd, uint32_t data) {
	uint8_t bit;
    _send_command(cmd);
    os_delay_us(DELAY_TDLY2);
    for (bit = 0; bit < 16; ++bit) {
        GPIO_SET(CLOCK_NUM, HIGH);
        if (data & 1)
            GPIO_SET(DATA_NUM, HIGH);
        else
            GPIO_SET(DATA_NUM, LOW);
        os_delay_us(DELAY_TSET1);
        GPIO_SET(CLOCK_NUM, LOW);
        os_delay_us(DELAY_THLD1);
        data >>= 1;
    }
    os_delay_us(DELAY_TDLY2);
}


// Send a command to the PIC that reads back a data value.
static ICACHE_FLASH_ATTR
uint32_t _send_read_command(uint8_t cmd) {
	uint8_t bit;
    uint32_t data = 0;
    _send_command(cmd);
    GPIO_SET(DATA_NUM, LOW);
    GPIO_INPUT(DATA_NUM);
    os_delay_us(DELAY_TDLY2);
    for (bit = 0; bit < 16; ++bit) {
        data >>= 1;
        GPIO_SET(CLOCK_NUM, HIGH);
        os_delay_us(DELAY_TDLY3);
        if (GPIO_GET(DATA_NUM)) {
            data |= 0x8000;
		}

        GPIO_SET(CLOCK_NUM, LOW);
        os_delay_us(DELAY_THLD1);
    }
    GPIO_OUTPUT(DATA_NUM);
    os_delay_us(DELAY_TDLY2);
    return data;
}


// Set the program counter to a specific "flat" address.
static ICACHE_FLASH_ATTR
void _set_program_counter(uint64_t addr) {
    if (addr >= dataStart && addr <= dataEnd) {
        // Data memory.
        addr -= dataStart;
        if (_state != STATE_PROGRAM || addr < _program_counter) {
            // Device is off, currently looking at configuration memory,
            // or the address is further back.  Reset the device.
            _exit_program_mode();
            _enter_program_mode();
        }
    } else if (addr >= configStart && addr <= configEnd) {
        // Configuration memory.
        addr -= configStart;
        if (_state == STATE_IDLE) {
            // Enter programming mode and switch to config memory.
            _enter_program_mode();
            _send_write_command(CMD_LOAD_CONFIG, 0);
            _state = STATE_CONFIG;
        } else if (_state == STATE_PROGRAM) {
            // Switch from program memory to config memory.
            _send_write_command(CMD_LOAD_CONFIG, 0);
            _state = STATE_CONFIG;
            _program_counter = 0;
        } else if (addr < _program_counter) {
            // Need to go backwards in config memory, so reset the device.
            _exit_program_mode();
            _enter_program_mode();
            _send_write_command(CMD_LOAD_CONFIG, 0);
            _state = STATE_CONFIG;
        }
    } else {
        // Program memory.
        if (_state != STATE_PROGRAM || addr < _program_counter) {
            // Device is off, currently looking at configuration memory,
            // or the address is further back.  Reset the device.
            _exit_program_mode();
            _enter_program_mode();
        }
    }
    while (_program_counter < addr) {
        _send_simple_command(CMD_INCREMENT_ADDRESS);
        ++_program_counter;
    }
}


// Sets the PC for "erase mode", which is activated by loading the
// data value 0x3FFF into location 0 of configuration memory.
static ICACHE_FLASH_ATTR
void _set_erase_program_counter() {
    // Forcibly reset the device so we know what state it is in.
    _exit_program_mode();
    _enter_program_mode();
    // Load 0x3FFF for the configuration.
    _send_write_command(CMD_LOAD_CONFIG, 0x3FFF);
    _state = STATE_CONFIG;
}


// Read a word from memory (program, config, or data depending upon addr).
// The start and stop bits will be stripped from the raw value from the PIC.
static ICACHE_FLASH_ATTR
uint32_t _read_word(uint64_t addr) {
    _set_program_counter(addr);
    if (addr >= dataStart && addr <= dataEnd)
        return (_send_read_command(CMD_READ_DATA_MEMORY) >> 1) & 0x00FF;
    else
        return (_send_read_command(CMD_READ_PROGRAM_MEMORY) >> 1) & 0x3FFF;
}


/*
 * Read a word from config memory using relative, non-flat, addressing.
 * Used by the "DEVICE" command to fetch information about devices whose
 * flat address ranges are presently unknown.
 */
static ICACHE_FLASH_ATTR
uint32_t _read_config_word(uint64_t addr) {

    if (_state == STATE_IDLE) {
        // Enter programming mode and switch to config memory.
        _enter_program_mode();
        _send_write_command(CMD_LOAD_CONFIG, 0);
        _state = STATE_CONFIG;
    } else if (_state == STATE_PROGRAM) {
        // Switch from program memory to config memory.
        _send_write_command(CMD_LOAD_CONFIG, 0);
        _state = STATE_CONFIG;
        _program_counter = 0;
    } else if (addr < _program_counter) {
        // Need to go backwards in config memory, so reset the device.
        _exit_program_mode();
        _enter_program_mode();
        _send_write_command(CMD_LOAD_CONFIG, 0);
        _state = STATE_CONFIG;
    }
    while (_program_counter < addr) {
        _send_simple_command(CMD_INCREMENT_ADDRESS);
        _program_counter++;
    }
    return (_send_read_command(CMD_READ_PROGRAM_MEMORY) >> 1) & 0x3FFF;
}


// Initialize device properties from the "devices" list and
// print them to the serial port.  Note: "dev" is in PROGMEM.
ICACHE_FLASH_ATTR
void _init_device(const struct deviceInfo *dev) {
    // Update the global device details.
    programEnd = dev->programSize - 1;
    configStart = dev->configStart;
    configEnd = configStart + dev->configSize - 1;
    dataStart = dev->dataStart;
    dataEnd = dataStart + dev->dataSize - 1;
    reservedStart = programEnd - dev->reservedWords + 1;
    reservedEnd = programEnd;
    configSave = dev->configSave;
    progFlashType = dev->progFlashType;
    dataFlashType = dev->dataFlashType;

    // Print the extra device information.
	os_printf("DeviceName: %s\r\n", dev->name);
    os_printf("ProgramRange: 0000-%04X\r\n", (uint32_t)programEnd);
    os_printf("ConfigRange: %04X-%04X\r\n", (uint32_t)configStart, 
			(uint32_t)configEnd);
    os_printf("ConfigSave: %02X\r\n", (uint16_t)configSave);
    os_printf("DataRange: %04X-%04X\r\n", (uint32_t)dataStart, 
			(uint32_t)dataEnd);
    if (reservedStart <= reservedEnd) {
        os_printf("ReservedRange: %04X-%04X\r\n", (uint32_t)reservedStart,
				(uint32_t)reservedEnd);
    }
}


// DEVICE command.
ICACHE_FLASH_ATTR
SPError pic_command_detect_device(const SPPacket *req) {
    // Make sure the device is reset before we start.
    _exit_program_mode();
	
	os_printf("Reading configuration...");

    // Read identifiers and configuration words from config memory.
    uint32_t userid0 = _read_config_word(DEV_USERID0);
    uint32_t userid1 = _read_config_word(DEV_USERID1);
    uint32_t userid2 = _read_config_word(DEV_USERID2);
    uint32_t userid3 = _read_config_word(DEV_USERID3);
    uint32_t deviceId = _read_config_word(DEV_ID);
    uint32_t configWord = _read_config_word(DEV_CONFIG_WORD);

    // If the device ID is all-zeroes or all-ones, then it could mean
    // one of the following:
    //
    // 1. There is no PIC in the programming socket.
    // 2. The VPP programming voltage is not available.
    // 3. Code protection is enabled and the PIC is unreadable.
    // 4. The PIC is an older model with no device identifier.
    //
    // Case 4 is the interesting one.  We look for any word in configuration
    // memory or the first 16 words of program memory that is non-zero.
    // If we find a non-zero word, we assume that we have a PIC but we
    // cannot detect what type it is.
    if (deviceId == 0 || deviceId == 0x3FFF) {
        uint32_t word = userid0 | userid1 | userid2 | userid3 | configWord;
        uint32_t addr = 0;
        while (!word && addr < 16) {
            word |= _read_word(addr);
            ++addr;
        }
        if (!word) {
            os_printf("ERROR\r\n");
            _exit_program_mode();
            return;
        }
        deviceId = 0;
    }
    os_printf("OK\r\n");
    os_printf("DeviceID: %02X\r\n", deviceId);
    // Find the device in the built-in list if we have details for it.
    int index = 0;

    for (;;) {
        const char *name = (const char *)
            (devices[index].name);
        if (!name) {
            index = -1;
            break;
        }
        int id = devices[index].deviceId;
        if (id == (deviceId & 0xFFE0)) {
			sp_tcpserver_response(SP_OK, name, os_strlen(name));
            break;
		}
        ++index;
    }	
    if (index >= 0) {
        _init_device(&(devices[index]));
    } 
	else {
		os_printf("No device detected\r\n");
        // Reset the global parameters to their defaults.  A separate
        // "SETDEVICE" command will be needed to set the correct values.
        programEnd    = 0x07FF;
        configStart   = 0x2000;
        configEnd     = 0x2007;
        dataStart     = 0x2100;
        dataEnd       = 0x217F;
        reservedStart = 0x0800;
        reservedEnd   = 0x07FF;
        configSave    = 0x0000;
        progFlashType = FLASH4;
        dataFlashType = EEPROM;
    }
    os_printf("ConfigWord: %02X\r\n", configWord);
    os_printf(".\r\n");
    // Don't need programming mode once the details have been read.
    _exit_program_mode();

    if (index < 0) {
		sp_tcpserver_response(SP_ERR_DEVICE_NOT_DETECTED, NULL, 0);
		return SP_ERR_DEVICE_NOT_DETECTED;
	}
	return SP_OK;
}


ICACHE_FLASH_ATTR
SPError pic_command_read(const SPPacket *req) {
    uint32_t start = bigendian_deserialize_uint32(req->body);
    uint32_t end = bigendian_deserialize_uint32(req->body + 8);
	uint32_t length = end - start;
    int count = 0;
    bool activity = true;
	char *buffer = (char*)os_zalloc(1024);
    while (start <= end) {
        uint32_t word = _read_word(start);
		bigendian_serialize_uint32(buffer + count * 4, word);
        ++start;
        ++count;
        if (count == 256) {
			sp_tcpserver_response(SP_STATUS_READ_MORE, buffer, 1024);		
		}
		if((count % 32) == 0) {
            // Toggle the activity LED to make it blink during long reads.
            activity ^= true;
			GPIO_SET(LED_NUM, activity);
        }
    }
	sp_tcpserver_response(SP_STATUS_READ_DONE, NULL, 0);		
	return SP_OK;
}

/*
// READBIN command.
void cmdReadBinary(const char *args)
{
    unsigned long start;
    unsigned long end;
    if (!parseCheckedRange(args, &start, &end)) {
        Serial.println("ERROR");
        return;
    }
    Serial.println("OK");
    int count = 0;
    bool activity = true;
    size_t offset = 0;
    while (start <= end) {
        unsigned int word = readWord(start);
        buffer[++offset] = (char)word;
        buffer[++offset] = (char)(word >> 8);
        if (offset >= BINARY_TRANSFER_MAX) {
            // Buffer is full - flush it to the host.
            buffer[0] = (char)offset;
            Serial.write((const uint8_t *)buffer, offset + 1);
            offset = 0;
        }
        ++start;
        ++count;
        if ((count % 64) == 0) {
            // Toggle the activity LED to make it blink during long reads.
            activity = !activity;
            if (activity)
                digitalWrite(PIN_ACTIVITY, HIGH);
            else
                digitalWrite(PIN_ACTIVITY, LOW);
        }
    }
    if (offset > 0) {
        // Flush the final packet before the terminator.
        buffer[0] = (char)offset;
        Serial.write((const uint8_t *)buffer, offset + 1);
    }
    // Write the terminator (a zero-length packet).
    Serial.write((uint8_t)0x00);
}
const char s_force[] PROGMEM = "FORCE";
// WRITE command.
void cmdWrite(const char *args)
{
    unsigned long addr;
    unsigned long limit;
    unsigned long value;
    int size;
    // Was the "FORCE" option given?
    int len = 0;
    while (args[len] != '\0' && args[len] != ' ' && args[len] != '\t')
        ++len;
    bool force = matchString(s_force, args, len);
    if (force) {
        args += len;
        while (*args == ' ' || *args == '\t')
            ++args;
    }
    size = parseHex(args, &addr);
    if (!size) {
        Serial.println("ERROR");
        return;
    }
    args += size;
    if (addr <= programEnd) {
        limit = programEnd;
    } else if (addr >= configStart && addr <= configEnd) {
        limit = configEnd;
    } else if (addr >= dataStart && addr <= dataEnd) {
        limit = dataEnd;
    } else {
        // Address is not within one of the valid ranges.
        Serial.println("ERROR");
        return;
    }
    int count = 0;
    for (;;) {
        while (*args == ' ' || *args == '\t')
            ++args;
        if (*args == '\0')
            break;
        if (*args == '-') {
            Serial.println("ERROR");
            return;
        }
        size = parseHex(args, &value);
        if (!size) {
            Serial.println("ERROR");
            return;
        }
        args += size;
        if (addr > limit) {
            // We've reached the limit of this memory area, so fail.
            Serial.println("ERROR");
            return;
        }
        if (!force) {
            if (!writeWord(addr, (unsigned int)value)) {
                // The actual write to the device failed.
                Serial.println("ERROR");
                return;
            }
        } else {
            if (!writeWordForced(addr, (unsigned int)value)) {
                // The actual write to the device failed.
                Serial.println("ERROR");
                return;
            }
        }
        ++addr;
        ++count;
    }
    if (!count) {
        // Missing word argument.
        Serial.println("ERROR");
    } else {
        Serial.println("OK");
    }
}
// Blocking serial read for use by WRITEBIN.
int readBlocking()
{
    while (!Serial.available())
        ;   // Do nothing.
    return Serial.read();
}
// WRITEBIN command.
void cmdWriteBinary(const char *args)
{
    unsigned long addr;
    unsigned long limit;
    int size;
    // Was the "FORCE" option given?
    int len = 0;
    while (args[len] != '\0' && args[len] != ' ' && args[len] != '\t')
        ++len;
    bool force = matchString(s_force, args, len);
    if (force) {
        args += len;
        while (*args == ' ' || *args == '\t')
            ++args;
    }
    size = parseHex(args, &addr);
    if (!size) {
        Serial.println("ERROR");
        return;
    }
    args += size;
    if (addr <= programEnd) {
        limit = programEnd;
    } else if (addr >= configStart && addr <= configEnd) {
        limit = configEnd;
    } else if (addr >= dataStart && addr <= dataEnd) {
        limit = dataEnd;
    } else {
        // Address is not within one of the valid ranges.
        Serial.println("ERROR");
        return;
    }
    Serial.println("OK");
    int count = 0;
    bool activity = true;
    for (;;) {
        // Read in the next binary packet.
        int len = readBlocking();
        while (len == 0x0A && count == 0) {
            // Skip 0x0A bytes before the first packet as they are
            // probably part of a CRLF pair rather than a packet length.
            len = readBlocking();
        }
        // Stop if we have a zero packet length - end of upload.
        if (!len)
            break;
        // Read the contents of the packet from the serial input stream.
        int offset = 0;
        while (offset < len) {
            if (offset < BINARY_TRANSFER_MAX) {
                buffer[offset++] = (char)readBlocking();
            } else {
                readBlocking();     // Packet is too big - discard extra bytes.
                ++offset;
            }
        }
        // Write the words to memory.
        for (int posn = 0; posn < (len - 1); posn += 2) {
            if (addr > limit) {
                // We've reached the limit of this memory area, so fail.
                Serial.println("ERROR");
                return;
            }
            unsigned int value =
                (((unsigned int)buffer[posn]) & 0xFF) |
                ((((unsigned int)buffer[posn + 1]) & 0xFF) << 8);
            if (!force) {
                if (!writeWord(addr, (unsigned int)value)) {
                    // The actual write to the device failed.
                    Serial.println("ERROR");
                    return;
                }
            } else {
                if (!writeWordForced(addr, (unsigned int)value)) {
                    // The actual write to the device failed.
                    Serial.println("ERROR");
                    return;
                }
            }
            ++addr;
            ++count;
            if ((count % 24) == 0) {
                // Toggle the activity LED to make it blink during long writes.
                activity = !activity;
                if (activity)
                    digitalWrite(PIN_ACTIVITY, HIGH);
                else
                    digitalWrite(PIN_ACTIVITY, LOW);
            }
        }
        // All words in this packet have been written successfully.
        Serial.println("OK");
    }
    Serial.println("OK");
} 
 */

ICACHE_FLASH_ATTR
void pic_initialize() {
	PIN_FUNC_SELECT(DATA_MUX, DATA_FUNC);
	PIN_PULLUP_EN(DATA_MUX);
	GPIO_OUTPUT(DATA_NUM);
}


ICACHE_FLASH_ATTR
void pic_shutdown() {
	// Do nothing here for now
}

