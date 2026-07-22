#include "I2CMasterController.h"

I2CMasterController::I2CMasterController(i2c_port_t port, gpio_num_t sda_pin, 
                                        gpio_num_t scl_pin, uint32_t frequency)
    : _port(port), _sdaPin(sda_pin), _sclPin(scl_pin), _frequency(frequency) {
}

I2CMasterController::~I2CMasterController() {
    i2c_driver_delete(_port);
}

bool I2CMasterController::begin() {
    i2c_config_t conf_master;
    conf_master.mode = I2C_MODE_MASTER;
    conf_master.sda_io_num = _sdaPin;
    conf_master.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf_master.scl_io_num = _sclPin;
    conf_master.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf_master.master.clk_speed = _frequency;
    conf_master.clk_flags = 0;
    
    esp_err_t ret = i2c_param_config(_port, &conf_master);
    if (ret != ESP_OK) {
        LOG_DEBUG("Log","I2C Master param config failed: %d", ret);
        return false;
    }
    
    ret = i2c_driver_install(_port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        LOG_DEBUG("Log","I2C Master driver install failed: %d", ret);
        return false;
    }
    
    LOG_DEBUG("Log", "I2C Master Controller initialized");
    return true;
}

bool I2CMasterController::scanDevices() {
    _discoveredDevices.clear();
    //LOG_DEBUG("Log", "Scanning I2C devices...");
    
    for (uint8_t address = 1; address < 127; address++) {
        if (pingDevice(address)) {
            SlaveDeviceInfo device;
            device.address = address;
            device.type = getDeviceType(address);
            device.name = "Device_0x" + String(address, HEX);
            
            _discoveredDevices.push_back(device);
            LOG_DEBUG("Log","Found device at 0x%02X - Type: %s", address, device.type.c_str());
        }
    }
    
    LOG_DEBUG("Log","Scan complete. Found %d devices.", _discoveredDevices.size());
    return !_discoveredDevices.empty();
}

// bool I2CMasterController::scanDevices(uint8_t address) {
//   ///  _discoveredDevices.clear();
//     LOG_DEBUG("Log", "Scanning I2C devices...");
    
//     if (pingDevice(address)) {
//         SlaveDeviceInfo device;
//         device.address = address;
//         device.type = getDeviceType(address);
//         device.name = "Device_0x" + String(address, HEX);
        
//         _discoveredDevices.push_back(device);
//         LOG_DEBUG("Log","Found device at 0x%02X - Type: %s", address, device.type.c_str());
//     }
   
//     LOG_DEBUG("Log","Scan complete. Found %d devices.", _discoveredDevices.size());
//     return !_discoveredDevices.empty();
// }

bool I2CMasterController::removeDeviceByAddress(uint8_t address) {
    auto it = std::find_if(_discoveredDevices.begin(), _discoveredDevices.end(),
                          [address](const SlaveDeviceInfo& device) {
                              return device.address == address;
                          });
    
    if (it != _discoveredDevices.end()) {
        _discoveredDevices.erase(it);
        LOG_DEBUG("Log", "I2C devices[0x%02X] offline, remove it", address);
        return true;
    }
    return false;
}

bool I2CMasterController::checkDeviceByAddress(uint8_t address) {
    auto it = std::find_if(_discoveredDevices.begin(), _discoveredDevices.end(),
                          [address](const SlaveDeviceInfo& device) {
                              return device.address == address;
                          });
    
    if (it != _discoveredDevices.end()) {
        //LOG_DEBUG("Log", "I2C devices[0x%02X] exsist! ", address);
        return true;
    }
    return false;
}


bool I2CMasterController::scanDevices(uint8_t address) {
  ///  _discoveredDevices.clear();
    // LOG_DEBUG("Log", "Scanning I2C devices...");
    // if (pingDevice(address)) {
    //     SlaveDeviceInfo device;
    //     device.address = address;
    //     device.type = getDeviceType(address);
    //     device.name = "Device_0x" + String(address, HEX);
    //     _discoveredDevices.push_back(device);
    //     LOG_DEBUG("Log","Found I2C devices at 0x%02X - Type: %s", address, device.type.c_str());
    // } else {
    //      removeDeviceByAddress(address);
    //      return false;
    // }
    // return true;

    //LOG_DEBUG("Log", "Scanning I2C devices...");
    uint32_t lastTime = millis();
    if (pingDevice(address)) {
        if (!checkDeviceByAddress(address)) {
            SlaveDeviceInfo device;
            device.address = address;
            //device.type = getDeviceType(address);
            device.name = "Device_0x" + String(address, HEX);
            _discoveredDevices.push_back(device);
            LOG_DEBUG("Log","Found I2C devices at 0x%02X - Type: %s, TIME=%d", address, device.type.c_str(), millis() - lastTime);
        }
    } else {
         removeDeviceByAddress(address);
         return false;
    }
    return true;
}


String I2CMasterController::sendCommand(uint8_t slaveAddress, const String& command, uint32_t timeout_ms) {
    if (!writeToDevice(slaveAddress, (const uint8_t*)command.c_str(), command.length(), timeout_ms)) {
        return "ERROR:Write Failed";
    }

    delay(10); // 等待从设备处理

    uint8_t buffer[BUFFER_LEN];
    memset(buffer, 0, BUFFER_LEN);
    
    // 尝试读取响应
    if (readFromDevice(slaveAddress, buffer, BUFFER_LEN - 1, timeout_ms)) {
        return String((char*)buffer);
    }
    
    return "ERROR:Read Failed";
}

bool I2CMasterController::sendCommandOnly(uint8_t slaveAddress, const String& command, uint32_t timeout_ms) {
    //LOG_DEBUG("Log","################# sendCommandOnly command.length()= %d", command.length());
    return writeToDevice(slaveAddress, (const uint8_t*)command.c_str(), command.length(), timeout_ms);   
    //return writeToDevice(slaveAddress, (const uint8_t*)command.c_str(), BUFFER_LEN - 1, timeout_ms);  
}


String I2CMasterController::readFromDevice(uint8_t slaveAddress, uint32_t timeout_ms) {
    uint8_t buffer[BUFFER_LEN]={0};
    //memset(buffer, 0, BUFFER_LEN);
    
    // 尝试读取响应
    if (readFromDevice(slaveAddress, buffer, BUFFER_LEN - 1, timeout_ms)) {
        return String((char*)buffer);
    }
    
    return "ERROR:Read Failed";
}


bool I2CMasterController::pingDevice(uint8_t slaveAddress) {
    // uint8_t dummy = 0;
    // esp_err_t ret = i2c_master_write_to_device(_port, slaveAddress, &dummy, 1, 
    //                                            50 / portTICK_PERIOD_MS);
    // return (ret == ESP_OK);
    //uint32_t lastTime = millis();
    esp_err_t ret = i2c_master_write_to_device(_port, slaveAddress, (const uint8_t*)(String("PING").c_str()), COMMAND_BUFFER_LEN, 0);
   // LOG_DEBUG("Log", "pingDevice TIME = %d", millis() - lastTime);
    return (ret == ESP_OK);
}

bool I2CMasterController::writeToDevice(uint8_t address, const uint8_t* data, size_t len, uint32_t timeout_ms) {
    esp_err_t ret = i2c_master_write_to_device(_port, address, data, len, 
                                               timeout_ms / portTICK_PERIOD_MS);
    return (ret == ESP_OK);
}

bool I2CMasterController::readFromDevice(uint8_t address, uint8_t* buffer, size_t len, uint32_t timeout_ms) {
    esp_err_t ret = i2c_master_read_from_device(_port, address, buffer, len, 
                                                timeout_ms / portTICK_PERIOD_MS);
    return (ret == ESP_OK);
}

String I2CMasterController::getDeviceType(uint8_t address) {
    // 发送识别命令来确定设备类型
    String response = sendCommand(address, "GETTYPE", 500);
    
    if (response.startsWith("SENSOR")) return "SENSOR";
    if (response.startsWith("DISPLAY")) return "DISPLAY"; 
    if (response.startsWith("MOTOR")) return "MOTOR";
    if (response.startsWith("PONG")) return "GENERIC";
    
    return "UNKNOWN";
}