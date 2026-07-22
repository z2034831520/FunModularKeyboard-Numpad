 #include <Arduino.h>

#include "configuration.h"
#include "display_task.h"
#include "interface_task.h"
#include "motor_task.h"

Configuration config;

#if SK_DISPLAY
static DisplayTask display_task(0);
static DisplayTask* display_task_p = &display_task;
#else
static DisplayTask* display_task_p = nullptr;
#endif
static MotorTask motor_task(1, config);


InterfaceTask interface_task(0, motor_task, display_task_p);

void setup() {
  #if SK_DISPLAY
  display_task.setLogger(&interface_task);
  display_task.begin();

  // Connect display to motor_task's knob state feed
  motor_task.addListener(display_task.getKnobStateQueue());
  #endif

  interface_task.begin();

  config.setLogger(&interface_task);
  config.loadFromDisk();

  interface_task.setConfiguration(&config);

  motor_task.setLogger(&interface_task);
  motor_task.begin();

  // Free up the Arduino loop task
  vTaskDelete(NULL);
}

void loop() {
  // char buf[50];
  // static uint32_t last_stack_debug;
  // if (millis() - last_stack_debug > 1000) {
  //   interface_task.log("Stack high water:");
  //   snprintf(buf, sizeof(buf), "  main: %d", uxTaskGetStackHighWaterMark(NULL));
  //   interface_task.log(buf);
  //   #if SK_DISPLAY
  //     snprintf(buf, sizeof(buf), "  display: %d", uxTaskGetStackHighWaterMark(display_task.getHandle()));
  //     interface_task.log(buf);
  //   #endif
  //   snprintf(buf, sizeof(buf), "  motor: %d", uxTaskGetStackHighWaterMark(motor_task.getHandle()));
  //   interface_task.log(buf);
  //   snprintf(buf, sizeof(buf), "  interface: %d", uxTaskGetStackHighWaterMark(interface_task.getHandle()));
  //   interface_task.log(buf);
  //   snprintf(buf, sizeof(buf), "Heap -- free: %d, largest: %d", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  //   interface_task.log(buf);
  //   last_stack_debug = millis();
  // }
}



// #include "driver/i2c.h"

// #define I2C_SLAVE_NUM  I2C_NUM_0

// // 从机引脚
// #define I2C_SLAVE_SDA  15
// #define I2C_SLAVE_SCL  8

// #define I2C_SLAVE_ADDR 0x08
// #define BUF_LEN        128

// void setup() {
//   Serial.begin(115200);
//   delay(1000);
//   Serial.println("\nESP32S3 I2C Master-Slave (Single Board) Test");

//   // 配置从机
//   i2c_config_t conf_slave;
//   conf_slave.mode = I2C_MODE_SLAVE;
//   conf_slave.sda_io_num = (gpio_num_t)I2C_SLAVE_SDA;
//   conf_slave.sda_pullup_en = GPIO_PULLUP_ENABLE;
//   conf_slave.scl_io_num = (gpio_num_t)I2C_SLAVE_SCL;
//   conf_slave.scl_pullup_en = GPIO_PULLUP_ENABLE;
//   conf_slave.slave.addr_10bit_en = 0;
//   conf_slave.slave.slave_addr = I2C_SLAVE_ADDR;
//   conf_slave.clk_flags = 0;
//   i2c_param_config(I2C_SLAVE_NUM, &conf_slave);
//   i2c_driver_install(I2C_SLAVE_NUM, I2C_MODE_SLAVE, BUF_LEN, BUF_LEN, 0);

//   Serial.println("I2C Master and Slave initialized.\n");
// }

// void loop() {
//   const char sendData[] = "Hello I2C Loop!";
//   uint8_t recvData[BUF_LEN];
//   memset(recvData, 0, sizeof(recvData));

//   // 从机读取数据
//   int len = i2c_slave_read_buffer(I2C_SLAVE_NUM, recvData, sizeof(recvData),
//                                   200 / portTICK_PERIOD_MS);

//   if (len > 0) {
//     Serial.print("[Slave] Received: ");
//     Serial.write(recvData, len);
//     Serial.println();

//     // 从机回发数据
//     String reply = String("ACK:okok66");
//     i2c_slave_write_buffer(I2C_SLAVE_NUM, (uint8_t*)reply.c_str(), reply.length(),
//                            100 / portTICK_PERIOD_MS);

//    }

//   delay(2000);
// }
