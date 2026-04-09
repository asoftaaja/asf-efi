#pragma once

#include <Arduino.h>

#define SERIAL_BAUD 115200

// Packet structure: [0xAA][LEN][CMD][DATA...][CRC8]
// LEN  = number of bytes that follow (CMD + DATA), not including start byte and LEN itself
// CRC8 = CRC-8/SMBUS over CMD + DATA bytes

#define PKT_START   0xAA

// Command IDs
#define CMD_READ_SENSORS    0x01  // PC→AVR: no payload; AVR→PC: sensor data
#define CMD_WRITE_MAP       0x02  // PC→AVR: 120-byte injection map
#define CMD_WRITE_PID       0x03  // PC→AVR: 12 bytes (kp, ki, kd as float32)
#define CMD_WRITE_PRESSURE  0x04  // PC→AVR: 10 bytes (low_bar, high_bar as float32 + threshold as uint16)
#define CMD_PUMP_PRIME      0x05  // PC→AVR: no payload
#define CMD_PUMP_SET        0x0D  // PC→AVR: 1-byte payload (1=on, 0=off) — manual test mode
#define CMD_PUMP_MODE       0x0E  // PC→AVR: 1-byte payload (0=PID, 1=always_on)
#define CMD_WRITE_IAT_CORR  0x08  // PC→AVR: 20 bytes (10 × uint16 Q8.8, 256 = 1.0)
#define CMD_WRITE_ET_CORR   0x09  // PC→AVR: 20 bytes (10 × uint16 Q8.8, 256 = 1.0)
#define CMD_READ_MAP        0x0A  // PC→AVR: no payload; AVR→PC: 120-byte injection map
#define CMD_WRITE_AXIS      0x0B  // PC→AVR: 34 bytes (12 × uint16 RPM + 5 × uint16 TPS per-mille)
#define CMD_READ_AXIS       0x0C  // PC→AVR: no payload; AVR→PC: same 34-byte layout
#define CMD_READ_PUMP_CONFIG  0x0F // PC→AVR: no payload; AVR→PC: 23 bytes — kp/ki/kd + low/high bar + threshold RPM + pump mode
#define CMD_READ_CORRECTIONS  0x10 // PC→AVR: no payload; AVR→PC: 40 bytes — 10 × uint16 IAT Q8.8 + 10 × uint16 ET Q8.8
#define CMD_ACK             0x06  // AVR→PC: acknowledged
#define CMD_NACK            0x07  // AVR→PC: rejected (bad CRC or unknown cmd)

void initComms();
void processSerial();    // call each loop iteration
void sendSensorData();   // sends CMD_READ_SENSORS response
void printSensorDebug(); // human-readable sensor dump to Serial
