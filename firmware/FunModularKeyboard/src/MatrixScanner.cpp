#include "MatrixScanner.h"

// // MatrixScanner::MatrixScanner(const uint8_t* row_pins, const uint8_t* col_pins, uint8_t rows, uint8_t cols) 
// //     : row_pins_(row_pins), col_pins_(col_pins), _rows(rows), _cols(cols) {
// MatrixScanner::MatrixScanner() {
//     row_pins_ = row_pins;
//     col_pins_ = col_pins;
//     for (int r = 0; r < PHYSICAL_KEY_ROW; r++) {
//         pinMode(row_pins[r], OUTPUT);
//         digitalWrite(row_pins[r], HIGH);
//     }
//     for (int c = 0; c < PHYSICAL_KEY_COL; c++) {
//         pinMode(col_pins[c], INPUT_PULLUP);
//     }
// }

// uint32_t MatrixScanner::scan(uint32_t &key_value) {
//     uint32_t current_state = 0;
//     for (int r = 0; r < PHYSICAL_KEY_ROW; r++) {
//         digitalWrite(row_pins_[r], LOW);
//         delayMicroseconds(10);
//         for (int c = 0; c < PHYSICAL_KEY_COL; c++) {
//             bool pressed = (digitalRead(col_pins_[c]) == LOW);
//             //LOG_DEBUG("Log","r = %d,c = %d,pressed= %d",r,c,pressed);
//             if (pressed) {
//                 delayMicroseconds(10000); // 消抖延时（通常5-20ms）
//                 pressed = (digitalRead(col_pins_[c]) == LOW); // 再次检测
//                 //LOG_DEBUG("Log","2222 pressed= %d",pressed);
//             }
//             current_state |= pressed << (r * PHYSICAL_KEY_COL + c);
//             //LOG_DEBUG("Log","current_state= %d",current_state);
//         }
//         digitalWrite(row_pins_[r], HIGH);
//     }
//     uint32_t changes = current_state ^ _last_state;
//    // LOG_DEBUG("Log","changes= %d",changes);
//     _last_state = current_state;
//     key_value = current_state;
//     return changes;
// }

// uint16_t MatrixScanner::scan(uint16_t &key_value) {
//     uint16_t current_state = 0;
//     uint32_t now = millis();
//     LOG_DEBUG("Log","now= %d", now);

//     // 1. 扫描原始状态
//     for (int r = 0; r < _rows; r++) {
//         digitalWrite(row_pins_[r], LOW);
//         delayMicroseconds(10);
//         for (int c = 0; c < _cols; c++) {
//             current_state |= (digitalRead(col_pins_[c]) == LOW) << (r * _cols + c);
//             LOG_DEBUG("Log","current_state = %d", current_state);
//         }
//         digitalWrite(row_pins_[r], HIGH);
//     }

//     // 2. 消抖处理
//     for (int i = 0; i < _rows * _cols; i++) {
//         bool current_pressed = (current_state & (1 << i));
//         bool last_pressed = (_last_state & (1 << i));
//         LOG_DEBUG("Log","current_pressed = %d, last_pressed= %d", current_pressed, last_pressed);
//         if (current_pressed != last_pressed) {
//             _debounce_time[i] = now; // 状态变化时重置计时器
//             LOG_DEBUG("Log","_debounce_time[i] = %d", _debounce_time[i]);
//         }
//         LOG_DEBUG("Log","now - _debounce_time[i] = %d", now - _debounce_time[i]);
//         // 仅当状态稳定超过 debounce_delay 时才更新
//         if ((now - _debounce_time[i]) >= _debounce_delay) {
//             if (current_pressed) {
//                 stable_state_ |= (1 << i);
//             } else {
//                 stable_state_ &= ~(1 << i);
//             }
//             LOG_DEBUG("Log","stable_state_ = %d",stable_state_);
//         }
//     }

//     uint16_t changes = stable_state_ ^ _last_state;
//     LOG_DEBUG("Log","changes = %d",changes);
//     _last_state = stable_state_;
//     key_value = stable_state_;
//     return changes;
// }


//MatrixScanner::~MatrixScanner() {}






#include "MatrixScanner.h"

MatrixScanner::MatrixScanner() 
    : row_pins_(row_pins), col_pins_(col_pins) {
    
    // 初始化行引脚(输出)
    for (int r = 0; r < PHYSICAL_KEY_ROW; r++) {
        pinMode(row_pins_[r], OUTPUT);
        digitalWrite(row_pins_[r], HIGH);
    }
    
    // 初始化列引脚(输入带上拉)
    for (int c = 0; c < PHYSICAL_KEY_COL; c++) {
        pinMode(col_pins_[c], INPUT_PULLUP);
    }
    
    // 初始化所有按键状态
    for (auto& key : key_states_) {
        key.state = KeyState::IDLE;
        key.timer = 0;
    }
}

MatrixScanner::~MatrixScanner() {}

uint32_t MatrixScanner::scan() {
    unsigned long current_time = millis();
    int key_index = 0;
    
    // 1. 扫描矩阵获取当前状态
    for (int r = 0; r < PHYSICAL_KEY_ROW; r++) {
        digitalWrite(row_pins_[r], LOW);
        delayMicroseconds(10); // 稳定信号
        
        for (int c = 0; c < PHYSICAL_KEY_COL; c++) {
            // int key_index = r * PHYSICAL_KEY_COL + c;
            //去除掉未使用的按键位置
            int phy_key_index = (r * PHYSICAL_KEY_COL + c);
            if ((phy_key_index == 2) || (phy_key_index == 3) ||
                (phy_key_index == 4) || (phy_key_index == 15)) {
                   continue;
            }

            bool pressed = (digitalRead(col_pins_[c]) == LOW);
            //LOG_DEBUG("Log","key_index=%d, pressed = %d",key_index, pressed);
            // 2. 处理每个按键的状态机
            KeyState& key = key_states_[key_index];
            
            //LOG_DEBUG("Log","key.state = %d", key.state);
            switch (key.state) {
                case KeyState::IDLE:
                    if (pressed) {
                        key.state = KeyState::DEBOUNCE_PRESS;
                        key.timer = current_time;
                    }
                    break;
                    
                case KeyState::DEBOUNCE_PRESS:
                    if ((long)(current_time - key.timer) >= DEBOUNCE_TIME_MS) {
                        if (pressed) {
                            key.state = KeyState::PRESSED;
                            stable_state_ |= (1 << key_index); // 标记为稳定按下
                            pressed_keys_ |= (1 << key_index);  // 记录按下事件
                        } else {
                            key.state = KeyState::IDLE; // 抖动，返回空闲
                        }
                    }
                    break;
                    
                case KeyState::PRESSED:
                    if (!pressed) {
                        key.state = KeyState::DEBOUNCE_RELEASE;
                        key.timer = current_time;
                    }
                    break;
                    
                case KeyState::DEBOUNCE_RELEASE:
                    if ((long)(current_time - key.timer) >= DEBOUNCE_TIME_MS) {
                        if (!pressed) {
                            key.state = KeyState::IDLE;
                            stable_state_ &= ~(1 << key_index); // 清除按下状态
                            released_keys_ |= (1 << key_index); // 记录释放事件
                        } else {
                            key.state = KeyState::PRESSED; // 抖动，返回按下状态
                        }
                    }
                    break;
            }
            key_index++;
        }
        
        digitalWrite(row_pins_[r], HIGH);
        delayMicroseconds(1); // 防止信号串扰
    }
    
     //LOG_DEBUG("Log","pressed_keys_ = %x,released_keys_ = %x", pressed_keys_, released_keys_);

    // 3. 返回变化的状态位
    uint32_t changes = (pressed_keys_ | released_keys_);
    pressed_keys_ = 0;  // 清除临时状态
    released_keys_ = 0;
    return changes;
}

uint32_t MatrixScanner::getStableState() {
    return stable_state_;
}

uint32_t MatrixScanner::getPressedKeys() {
    return pressed_keys_;
}

uint32_t MatrixScanner::getReleasedKeys() {
    return released_keys_;
}