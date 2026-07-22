// I2CSlaveDevice.h
#ifndef I2C_SLAVE_DEVICE_H
#define I2C_SLAVE_DEVICE_H

#include "driver/i2c.h"
#include <Arduino.h>
#include "logger.h"

#define I2C_SLAVE_NUM  I2C_NUM_0
#define I2C_SLAVE_SDA  GPIO_NUM_15
#define I2C_SLAVE_SCL  GPIO_NUM_8
#define I2C_SLAVE_ADDR 0x08

class I2CSlaveDevice {
public:
    enum DeviceType {
        SENSOR_MODULE = 0x08,
        DISPLAY_MODULE = 0x09,
        MOTOR_MODULE = 0x0A
    };

    I2CSlaveDevice(i2c_port_t port = I2C_SLAVE_NUM, uint8_t address = I2C_SLAVE_ADDR, gpio_num_t sda_pin = I2C_SLAVE_SDA, gpio_num_t scl_pin = I2C_SLAVE_SCL, DeviceType type = MOTOR_MODULE);
    ~I2CSlaveDevice();
    
    bool begin();
    void process(String out_data, String &command, String &response);
    void sendResponse(const String& data);
    String getReceivedData() const { return String((char*)_receiveBuffer); }
    DeviceType getType() const { return _type; }
    uint8_t getAddress() const { return _address; }
    
    Logger* logger_;
    void setLogger(Logger* logger);
    void log(const char* msg);

private:
    i2c_port_t _port;
    uint8_t _address;
    gpio_num_t _sdaPin;
    gpio_num_t _sclPin;
    DeviceType _type;
   
    static const uint16_t COMMAND_BUFFER_LEN = 32;
    static const uint16_t BUFFER_LEN = 128;
    uint8_t _receiveBuffer[COMMAND_BUFFER_LEN];
    uint8_t _sendBuffer[BUFFER_LEN];
    
    void handleCommand(const String& command, String out_data, String &response);
};

#endif