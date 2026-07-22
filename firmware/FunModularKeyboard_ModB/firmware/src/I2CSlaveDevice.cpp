// I2CSlaveDevice.cpp
#include "I2CSlaveDevice.h"

I2CSlaveDevice::I2CSlaveDevice(i2c_port_t port, uint8_t address, 
                               gpio_num_t sda_pin, gpio_num_t scl_pin, 
                               DeviceType type)
    : _port(port), _address(address), _sdaPin(sda_pin), _sclPin(scl_pin), _type(type) {
    memset(_receiveBuffer, 0, COMMAND_BUFFER_LEN);
    memset(_sendBuffer, 0, BUFFER_LEN);
}

I2CSlaveDevice::~I2CSlaveDevice() {
    i2c_driver_delete(_port);
}

bool I2CSlaveDevice::begin() {
    i2c_config_t conf_slave;
    conf_slave.mode = I2C_MODE_SLAVE;
    conf_slave.sda_io_num = _sdaPin;
    conf_slave.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf_slave.scl_io_num = _sclPin;
    conf_slave.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf_slave.slave.addr_10bit_en = 0;
    conf_slave.slave.slave_addr = _address;
    conf_slave.clk_flags = 0;
    
    esp_err_t ret = i2c_param_config(_port, &conf_slave);
    if (ret != ESP_OK) {
        //Serial.printf("I2C Slave param config failed: %d\r\n", ret);
        return false;
    }
    
    ret = i2c_driver_install(_port, I2C_MODE_SLAVE, COMMAND_BUFFER_LEN, BUFFER_LEN, 0);
    if (ret != ESP_OK) {
        //Serial.printf("I2C Slave driver install failed: %d\r\n", ret);
        return false;
    }
    
    //Serial.printf("I2C Slave Device [0x%02X] initialized on port %d\r\n", _address, _port);
    return true;
}

void I2CSlaveDevice::process(String out_data, String &command, String &response) {
    memset(_receiveBuffer, 0, COMMAND_BUFFER_LEN);
    int len = i2c_slave_read_buffer(_port, _receiveBuffer, COMMAND_BUFFER_LEN - 1, 
                                    30 / portTICK_PERIOD_MS);
    // int len = i2c_slave_read_buffer(_port, _receiveBuffer, String("GETDATA").length(), 
    //                                 200 / portTICK_PERIOD_MS);    
    
    if (len > 0) {
        command = String((char*)_receiveBuffer);
        // char buf_[128] = {0};
        // snprintf(buf_, sizeof(buf_), "process [Slave 0x%02X] len =%d, Received: %s\r\n", _address,len, command.c_str());
        // log(buf_);
        handleCommand(command, out_data, response);
    }
}

void I2CSlaveDevice::sendResponse(const String& data) {
    if (data.length() > 0) {
        i2c_slave_write_buffer(_port, (uint8_t*)data.c_str(), data.length(),
                               30 / portTICK_PERIOD_MS);
    }
}

void I2CSlaveDevice::handleCommand(const String& command, String out_data, String &response) {
    ///String response;
    //Serial.printf("########## handleCommand command %s\r\n", command.c_str());
    if (command.startsWith("GETDATA")) {
        switch (_type) {
            case SENSOR_MODULE: {
                response = "[I2C_RESPONSE]SENSOR:Temp=25.6,Humidity=60%";
                break;
            }    
            case DISPLAY_MODULE: {
                response = "[I2C_RESPONSE]DISPLAY:Ready";
                break;
            }
            case MOTOR_MODULE: {
                char temp[128]={0};
                sprintf(temp,"[I2C_RESPONSE]MODB:%s/over",out_data.c_str());//以/over作为协议字段结束
                //response = "[I2C_RESPONSE] MOTOR:Position=180";
                response = temp;
                break;
            }
            default:{
                response = "[I2C_RESPONSE]UNKNOWN_DEVICE";
                break;
            }
        }
    } else if (command.startsWith("PING")) {
        response = "[I2C_RESPONSE]PONG:" + String(_address, HEX);
    } else if (command.startsWith("GETSTATUS")) {
        response = "[I2C_RESPONSE]STATUS:OK";
    } else {
        response = "[I2C_RESPONSE]ERROR:Unknown Command";
    }
    
    sendResponse(response);
   // Serial.printf("[Slave 0x%02X] Sent: %s\r\n", _address, response.c_str());
}

void I2CSlaveDevice::setLogger(Logger* logger) {
    logger_ = logger;
}

void I2CSlaveDevice::log(const char* msg) {
    if (logger_ != nullptr) {
        logger_->log(msg);
    }
}
