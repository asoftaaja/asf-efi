#pragma once
/* Mock Arduino EEPROM library.
 * Uses a plain byte array so eeprom_map.cpp compiles and save/load functions
 * can be tested without real EEPROM hardware.
 */
#include <stdint.h>
#include <string.h>

#define EEPROM_SIZE 1024

extern uint8_t mock_eeprom[EEPROM_SIZE];

class EEPROMClass {
public:
    uint8_t read(int addr) {
        return (addr >= 0 && addr < EEPROM_SIZE) ? mock_eeprom[addr] : 0;
    }
    void write(int addr, uint8_t val) {
        if (addr >= 0 && addr < EEPROM_SIZE) mock_eeprom[addr] = val;
    }
    void update(int addr, uint8_t val) { write(addr, val); }

    template<typename T>
    T& get(int addr, T& t) {
        if (addr >= 0 && addr + (int)sizeof(T) <= EEPROM_SIZE)
            memcpy(&t, mock_eeprom + addr, sizeof(T));
        return t;
    }
    template<typename T>
    const T& put(int addr, const T& t) {
        if (addr >= 0 && addr + (int)sizeof(T) <= EEPROM_SIZE)
            memcpy(mock_eeprom + addr, &t, sizeof(T));
        return t;
    }
};

extern EEPROMClass EEPROM;
