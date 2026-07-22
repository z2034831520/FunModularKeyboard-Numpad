#if SK_LEDS
#include <FastLED.h>
#endif

#if SK_STRAIN
#include <HX711.h>
#endif

#if SK_ALS
#include <Adafruit_VEML7700.h>
#endif

#include "interface_task.h"
#include "semaphore_guard.h"
#include "util.h"

#if SK_LEDS
CRGB leds[NUM_LEDS];
static const CRGB kAlwaysOnLedColor = CRGB(255, 69, 0);
static const CRGB kColorTurnPalette[] = {
    CRGB::Black,
    CRGB::White,
    CRGB(192, 192, 192),
    CRGB::Red,
    CRGB(128, 0, 0),
    kAlwaysOnLedColor,
    CRGB::Orange,
    CRGB(210, 105, 30),
    CRGB::Yellow,
    CRGB(255, 215, 0),
    CRGB(173, 255, 47),
    CRGB::Green,
    CRGB(127, 255, 212),
    CRGB(0, 128, 128),
    CRGB::Cyan,
    CRGB(0, 191, 255),
    CRGB::Blue,
    CRGB(0, 0, 128),
    CRGB(75, 0, 130),
    CRGB::Purple,
    CRGB(148, 0, 211),
    CRGB::Magenta,
    CRGB(255, 105, 180),
    CRGB(255, 244, 229),
};
static const int32_t kDefaultLedColorIndex = 5;
static const uint32_t kLedColorSaveDelayMs = 750;
static const uint32_t kLedColorSaveIdleMs = 3000;

static int32_t normalizeLedColorIndex(int32_t color_index) {
    const int32_t palette_size = COUNT_OF(kColorTurnPalette);
    color_index %= palette_size;
    if (color_index < 0) {
        color_index += palette_size;
    }
    return color_index;
}

static CRGB colorForLedIndex(int32_t color_index) {
    return kColorTurnPalette[normalizeLedColorIndex(color_index)];
}
#endif

#if SK_STRAIN
HX711 scale;
#endif

#if SK_ALS
Adafruit_VEML7700 veml = Adafruit_VEML7700();
#endif

static PB_SmartKnobConfig configs[] = {
    // int32_t position;
    // float sub_position_unit;
    // uint8_t position_nonce;
    // int32_t min_position;
    // int32_t max_position;
    // float position_width_radians;
    // float detent_strength_unit;
    // float endstop_strength_unit;
    // float snap_point;
    // char text[51];
    // pb_size_t detent_positions_count;
    // int32_t detent_positions[5];
    // float snap_point_bias;
    // int8_t led_hue;

    // {
    //     0,
    //     0,
    //     0,
    //     0,
    //     -1, // max position < min position indicates no bounds
    //     10 * PI / 180,
    //     0,
    //     1,
    //     1.1,
    //     "Unbounded\nNo detents",
    //     0,
    //     {},
    //     0,
    //     200,
    // },
    // {
    //     0,
    //     0,
    //     1,
    //     0,
    //     10,
    //     10 * PI / 180,
    //     0,
    //     1,
    //     1.1,
    //     "Bounded 0-10\nNo detents",
    //     0,
    //     {},
    //     0,
    //     0,
    // },
    // {
    //     0,
    //     0,
    //     2,
    //     0,
    //     72,
    //     10 * PI / 180,
    //     0,
    //     1,
    //     1.1,
    //     "Multi-rev\nNo detents",
    //     0,
    //     {},
    //     0,
    //     73,
    // },
    // {
    //     0,
    //     0,
    //     3,
    //     0,
    //     1,
    //     60 * PI / 180,
    //     1,
    //     1,
    //     0.55, // Note the snap point is slightly past the midpoint (0.5); compare to normal detents which use a snap point *past* the next value (i.e. > 1)
    //     "On/off\nStrong detent",
    //     0,
    //     {},
    //     0,
    //     157,
    // },
    // {
    //     0,
    //     0,
    //     4,
    //     0,
    //     0,
    //     60 * PI / 180,
    //     0.01,
    //     0.6,
    //     1.1,
    //     "Return-to-center",
    //     0,
    //     {},
    //     0,
    //     45,
    // },
    // {
    //     127,
    //     0,
    //     5,
    //     0,
    //     255,
    //     1 * PI / 180,
    //     0,
    //     1,
    //     1.1,
    //     "Fine values\nNo detents",
    //     0,
    //     {},
    //     0,
    //     219,
    // },
    // {
    //     127,
    //     0,
    //     5,
    //     0,
    //     255,
    //     1 * PI / 180,
    //     1,
    //     1,
    //     1.1,
    //     "Fine values\nWith detents",
    //     0,
    //     {},
    //     0,
    //     25,
    // },
    // {
    //     0,
    //     0,
    //     6,
    //     0,
    //     31,
    //     8.225806452 * PI / 180,
    //     2,
    //     1,
    //     1.1,
    //     "Coarse values\nStrong detents",
    //     0,
    //     {},
    //     0,
    //     200,
    // },
    // {
    //     0,
    //     0,
    //     6,
    //     0,
    //     31,
    //     8.225806452 * PI / 180,
    //     0.2,
    //     1,
    //     1.1,
    //     "Coarse values\nWeak detents",
    //     0,
    //     {},
    //     0,
    //     0,
    // },
    // {
    //     0,
    //     0,
    //     7,
    //     0,
    //     31,
    //     7 * PI / 180,
    //     2.5,
    //     1,
    //     0.7,
    //     "Magnetic detents",
    //     4,
    //     {2, 10, 21, 22},
    //     0,
    //     73,
    // },
    // {
    //     0,
    //     0,
    //     8,
    //     -6,
    //     6,
    //     60 * PI / 180,
    //     1,
    //     1,
    //     0.55,
    //     "Return-to-center\nwith detents",
    //     0,
    //     {},
    //     0.4,
    //     157,
    // },
  


    // {
    //     0,
    //     0,
    //     6,
    //     0,
    //     100,
    //     3.6 * PI / 180,
    //     2,
    //     1,
    //     1.1,
    //     "Coarse values\nStrong detents",
    //     0,
    //     {},
    //     0,
    //     60,
    // },
       {
        0,
        0,
        6,
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max(),
        1.8 * PI / 180,
        2,
        1,
        1.1,
        "Coarse values\nStrong detents",
        0,
        {},
        0,
        60,
    },
    {
        0,
        0,
        6,
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max(),
        3.6 * PI / 180,
        0.5,
        1,
        1.1,
        "Coarse values\nNormal detents",
        0,
        {},
        0,
        200,
    },
    {
        0,
        0,
        6,
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max(),
        7.2 * PI / 180,
        0.2,
        1,
        1.1,
        "Coarse values\nWeak detents",
        0,
        {},
        0,
        0,
    },
    {
        0,
        0,
        6,
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max(),
        7.5 * PI / 180,
        5,
        1,
        1.1,
        "Color Turn\nselect color with detents",
        0,
        {},
        0,
        0,
    },
};

static const int32_t kColorTurnConfigIndex = COUNT_OF(configs) - 1;

InterfaceTask::InterfaceTask(const uint8_t task_core, MotorTask& motor_task, DisplayTask* display_task) : 
        Task("Interface", 3400, 1, task_core),
        stream_(),
        motor_task_(motor_task),
        display_task_(display_task),
        plaintext_protocol_(stream_, [this] () {
            motor_task_.runCalibration();
        }),
        proto_protocol_(stream_, [this] (PB_SmartKnobConfig& config) {
            applyConfig(config, true);
        }) {
    #if SK_DISPLAY
        assert(display_task != nullptr);
    #endif


    log_queue_ = xQueueCreate(10, sizeof(std::string *));
    assert(log_queue_ != NULL);

    knob_state_queue_ = xQueueCreate(1, sizeof(PB_SmartKnobState));
    assert(knob_state_queue_ != NULL);

    mutex_ = xSemaphoreCreateMutex();
    assert(mutex_ != NULL);

    selected_led_color_index_ = kDefaultLedColorIndex;
}

InterfaceTask::~InterfaceTask() {
    vSemaphoreDelete(mutex_);
}

void InterfaceTask::run() {
    stream_.begin();
    
    #if SK_LEDS
        FastLED.addLeds<SK6812, PIN_LED_DATA, GRB>(leds, NUM_LEDS);
    #endif

    #if SK_ALS && PIN_SDA >= 0 && PIN_SCL >= 0
        Wire.begin(PIN_SDA, PIN_SCL);
        Wire.setClock(400000);
    #endif
    #if SK_STRAIN
        scale.begin(PIN_STRAIN_DO, PIN_STRAIN_SCK);
    #endif

    #if SK_ALS
        if (veml.begin()) {
            veml.setGain(VEML7700_GAIN_2);
            veml.setIntegrationTime(VEML7700_IT_400MS);
        } else {
            log("ALS sensor not found!");
        }
    #endif

    applyConfig(configs[0], false);
    motor_task_.addListener(knob_state_queue_);

    plaintext_protocol_.init([this] () {
        changeConfig(true);
    }, [this] () {
        if (!configuration_loaded_) {
            return;
        }
        if (strain_calibration_step_ == 0) {
            log("Strain calibration step 1: Don't touch the knob, then press 'S' again");
            strain_calibration_step_ = 1;
        } else if (strain_calibration_step_ == 1) {
            configuration_value_.strain.idle_value = strain_reading_;
            snprintf(buf_, sizeof(buf_), "  idle_value=%d", configuration_value_.strain.idle_value);
            log(buf_);
            log("Strain calibration step 2: Push and hold down the knob with medium pressure, and press 'S' again");
            strain_calibration_step_ = 2;
        } else if (strain_calibration_step_ == 2) {
            configuration_value_.strain.press_delta = strain_reading_ - configuration_value_.strain.idle_value;
            configuration_value_.has_strain = true;
            snprintf(buf_, sizeof(buf_), "  press_delta=%d", configuration_value_.strain.press_delta);
            log(buf_);
            log("Strain calibration complete! Saving...");
            strain_calibration_step_ = 0;
            if (configuration_->setStrainCalibrationAndSave(configuration_value_.strain)) {
                log("  Saved!");
            } else {
                log("  FAILED to save config!!!");
            }
        }
    });

    // Start in legacy protocol mode
    current_protocol_ = &plaintext_protocol_;

    ProtocolChangeCallback protocol_change_callback = [this] (uint8_t protocol) {
        switch (protocol) {
            case SERIAL_PROTOCOL_LEGACY:
                current_protocol_ = &plaintext_protocol_;
                break;
            case SERIAL_PROTOCOL_PROTO:
                current_protocol_ = &proto_protocol_;
                break;
            default:
                log("Unknown protocol requested");
                break;
        }
    };

    plaintext_protocol_.setProtocolChangeCallback(protocol_change_callback);
    proto_protocol_.setProtocolChangeCallback(protocol_change_callback);


    //i2cSlave_.setLogger(logger_);

    if (i2cSlave_.begin()) {
        log("I2C Slave started successfully");
    } else {
        log("Failed to initialize I2C Slave");
    }
 
    // Interface loop:
    char out_current_position[32] = {0};
    while (1) {
         uint32_t lasttTime = millis();
        if (xQueueReceive(knob_state_queue_, &latest_state_, 30) == pdTRUE) {
            ///publishState();
            static bool has_previous_knob_state = false;
            static int32_t previous_position = 0;
            static float previous_sub_position = 0;
            if (!has_previous_knob_state
                    || latest_state_.current_position != previous_position
                    || fabsf(latest_state_.sub_position_unit - previous_sub_position) > 0.001f) {
                last_knob_state_ms_ = millis();
                previous_position = latest_state_.current_position;
                previous_sub_position = latest_state_.sub_position_unit;
                has_previous_knob_state = true;
            }

            //latest_state_
            // char buf_[256] = {0};
            // snprintf(buf_, sizeof(buf_), "state.current_position=%d,sub_position_unit=%f",latest_state_.current_position, latest_state_.sub_position_unit);
            // log(buf_);
            memset(out_current_position, 0 ,sizeof(out_current_position));
            sprintf(out_current_position, "Position=%d",latest_state_.current_position);
            String command, response;
            i2cSlave_.process(out_current_position, command, response);
            //log(command.c_str());
            //log(response.c_str());
            
        }
  
        // char buf_[256] = {0};
        // snprintf(buf_, sizeof(buf_), "TIME=%d",millis() - lasttTime);
        // log(buf_);

        current_protocol_->loop();

        std::string* log_string;
        while (xQueueReceive(log_queue_, &log_string, 0) == pdTRUE) {
            current_protocol_->log(log_string->c_str());
            delete log_string;
        }

        updateHardware();

        if (!configuration_loaded_) {
            SemaphoreGuard lock(mutex_);
            if (configuration_ != nullptr) {
                configuration_value_ = configuration_->get();
                if (configuration_value_.has_led_color_index) {
                    selected_led_color_index_ = normalizeLedColorIndex((int32_t)configuration_value_.led_color_index);
                } else {
                    selected_led_color_index_ = kDefaultLedColorIndex;
                }
                configuration_loaded_ = true;
            }
        }

        delay(1);
    }
}

void InterfaceTask::log(const char* msg) {
    // Allocate a string for the duration it's in the queue; it is free'd by the queue consumer
    std::string* msg_str = new std::string(msg);

    // Put string in queue (or drop if full to avoid blocking)
    xQueueSendToBack(log_queue_, &msg_str, 0);
}

void InterfaceTask::changeConfig(bool next) {
    if (next) {
        current_config_ = (current_config_ + 1) % COUNT_OF(configs);
    } else {
        if (current_config_ == 0) {
            current_config_ = COUNT_OF(configs) - 1;
        } else {
            current_config_ --;
        }
    }
    
    snprintf(buf_, sizeof(buf_), "Changing config to %d -- %s", current_config_, configs[current_config_].text);
    log(buf_);
    PB_SmartKnobConfig config = configs[current_config_];
    if (current_config_ == kColorTurnConfigIndex) {
        config.position = normalizeLedColorIndex(selected_led_color_index_);
        config.sub_position_unit = 0;
        config.position_nonce = press_count_;
    }
    applyConfig(config, false);
}

//调整led显示顺序
// 1-2
// 2-1
// 3-8
// 4-7
// 5-6
// 6-5
// 7-4
// 8-3
int InterfaceTask::switchLEDs(int index) {
    
    switch(index) {
      case 0: return 1; break;
      case 1: return 0; break;
      case 2: return 7; break;
      case 3: return 6; break;
      case 4: return 5; break;
      case 5: return 4; break;
      case 6: return 3; break;
      case 7: return 2; break;
    }

        return index;
}

void InterfaceTask::persistLedColorSelection(bool force) {
    if (!led_color_dirty_ || !configuration_loaded_ || configuration_ == nullptr) {
        return;
    }

    const uint32_t now = millis();
    if (!force) {
        if (now - last_led_color_change_ms_ < kLedColorSaveDelayMs) {
            return;
        }
        if (now - last_knob_state_ms_ < kLedColorSaveIdleMs) {
            return;
        }
    }

    const uint32_t color_index = (uint32_t)normalizeLedColorIndex(selected_led_color_index_);
    if (configuration_value_.has_led_color_index && configuration_value_.led_color_index == color_index) {
        led_color_dirty_ = false;
        return;
    }

    if (configuration_->setLedColorAndSave(color_index)) {
        configuration_value_.led_color_index = color_index;
        configuration_value_.has_led_color_index = true;
        led_color_dirty_ = false;
    } else {
        log("FAILED to save LED color!!!");
    }
}

void InterfaceTask::updateHardware() {
    // How far button is pressed, in range [0, 1]
    float press_value_unit = 0;

    #if SK_ALS
        const float LUX_ALPHA = 0.005;
        static float lux_avg;
        float lux = veml.readLux();
        lux_avg = lux * LUX_ALPHA + lux_avg * (1 - LUX_ALPHA);
        static uint32_t last_als;
        if (millis() - last_als > 1000 && strain_calibration_step_ == 0) {
            snprintf(buf_, sizeof(buf_), "millilux: %.2f", lux*1000);
            log(buf_);
            last_als = millis();
        }
    #endif

    static bool pressed;
    #if SK_STRAIN
        if (scale.wait_ready_timeout(100)) {
            strain_reading_ = scale.read();

            static uint32_t last_reading_display;
            if (millis() - last_reading_display > 1000 && strain_calibration_step_ == 0) {
                //snprintf(buf_, sizeof(buf_), "HX711 reading: %d", strain_reading_);
                //log(buf_);
                last_reading_display = millis();
            }
           // snprintf(buf_, sizeof(buf_), "00000000000 configuration_loaded_: %d,configuration_value_.has_strain%d,strain_calibration_step_%d", configuration_loaded_,configuration_value_.has_strain,strain_calibration_step_);
            //log(buf_);

            if (configuration_loaded_ && configuration_value_.has_strain && strain_calibration_step_ == 0) {
                // TODO: calibrate and track (long term moving average) idle point (lower)
                press_value_unit = lerp(strain_reading_, configuration_value_.strain.idle_value, configuration_value_.strain.idle_value + configuration_value_.strain.press_delta, 0, 1);
                // Ignore readings that are way out of expected bounds
                if (-1 < press_value_unit && press_value_unit < 2) {
                    static uint8_t press_readings;
                    if (!pressed && press_value_unit > 1) {
                        press_readings++;
                        if (press_readings > 2) {
                            motor_task_.playHaptic(true);
                            pressed = true;
                            press_count_++;
                            publishState();
                            if (!remote_controlled_) {
                                changeConfig(true);
                            }
                        }
                    } else if (pressed && press_value_unit < 0.5) {
                        press_readings++;
                        if (press_readings > 2) {
                            motor_task_.playHaptic(false);
                            pressed = false;
                        }
                    } else {
                        press_readings = 0;
                    }
                }
            }
        } else {
            log("HX711 not found.");

            #if SK_LEDS
                for (uint8_t i = 0; i < NUM_LEDS; i++) {
                    leds[i] = CRGB::Red;
                }
                FastLED.show();
            #endif
        }
    #endif

    ///uint16_t brightness = UINT16_MAX;
    uint16_t brightness = UINT16_MAX;
    // TODO: brightness scale factor should be configurable (depends on reflectivity of surface)
    #if SK_ALS
        brightness = (uint16_t)CLAMP(lux_avg * 13000, (float)1280, (float)UINT16_MAX);
    #endif

    #if SK_DISPLAY
        display_task_->setBrightness(brightness); // TODO: apply gamma correction
    #endif

    #if SK_LEDS
        if (current_config_ == kColorTurnConfigIndex) {
            int32_t color_index = normalizeLedColorIndex(latest_state_.current_position);
            if (selected_led_color_index_ != color_index) {
                selected_led_color_index_ = color_index;
                led_color_dirty_ = true;
                last_led_color_change_ms_ = millis();
            }
        }

        persistLedColorSelection(false);

        CRGB led_color = colorForLedIndex(selected_led_color_index_);

        for (uint8_t i = 0; i < NUM_LEDS; i++) {
            leds[i] = led_color;
        }

        FastLED.show();
    #endif
}

void InterfaceTask::setConfiguration(Configuration* configuration) {
    SemaphoreGuard lock(mutex_);
    configuration_ = configuration;
}

void InterfaceTask::publishState() {
    // Apply local state before publishing to serial
    latest_state_.press_nonce = press_count_;
    current_protocol_->handleState(latest_state_);
}

void InterfaceTask::applyConfig(PB_SmartKnobConfig& config, bool from_remote) {
    remote_controlled_ = from_remote;
    latest_config_ = config;
    motor_task_.setConfig(config);
}
