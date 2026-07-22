// I2CMasterController.h
#ifndef I2C_MASTER_CONTROLLER_H
#define I2C_MASTER_CONTROLLER_H

#include "driver/i2c.h"
#include <Arduino.h>
#include <vector>
#include "LogManager.h"

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA GPIO_NUM_15
#define I2C_MASTER_SCL GPIO_NUM_8
#define I2C_MASTER_FREQ 100000

struct SlaveDeviceInfo {
    uint8_t address;
    String name;
    String type;
};

class I2CMasterController {
public:
    I2CMasterController(i2c_port_t port = I2C_MASTER_NUM, gpio_num_t sda_pin = I2C_MASTER_SDA, gpio_num_t scl_pin = I2C_MASTER_SCL, uint32_t frequency = I2C_MASTER_FREQ);
    ~I2CMasterController();
    
    bool begin();
    bool scanDevices();
    bool scanDevices(uint8_t address);
    String sendCommand(uint8_t slaveAddress, const String& command, uint32_t timeout_ms = 0);
    bool removeDeviceByAddress(uint8_t address);
    bool checkDeviceByAddress(uint8_t address);
    bool sendCommandOnly(uint8_t slaveAddress, const String& command, uint32_t timeout_ms = 0);
    String readFromDevice(uint8_t slaveAddress, uint32_t timeout_ms = 0);
    bool pingDevice(uint8_t slaveAddress);
    std::vector<SlaveDeviceInfo> getDiscoveredDevices() const { return _discoveredDevices; }
    
private:
    i2c_port_t _port;
    gpio_num_t _sdaPin;
    gpio_num_t _sclPin;
    uint32_t _frequency;
    std::vector<SlaveDeviceInfo> _discoveredDevices;
    
    static const uint16_t COMMAND_BUFFER_LEN = 32;
    static const uint16_t BUFFER_LEN = 128;    
    
    bool writeToDevice(uint8_t address, const uint8_t* data, size_t len, uint32_t timeout_ms);
    bool readFromDevice(uint8_t address, uint8_t* buffer, size_t len, uint32_t timeout_ms);
    String getDeviceType(uint8_t address);
};

#endif