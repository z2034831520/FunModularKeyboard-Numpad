#include "Mic.h"

Mic::Mic()
{
}

Mic::~Mic()
{
    End();
}

bool Mic::Begin()
{
    if (_initialized)
        return true;

    // 重置 RX 状态机
    //  i2s_zero_dma_buffer(I2S_NUM_0);
    //  i2s_stop(I2S_NUM_0);
    //  ets_delay_us(100);
    //  i2s_start(I2S_NUM_0);

    // I2S 配置
    i2s_config_ = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUFFER_SIZE / 4, // 缓冲区长度（可调整）
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0};

    // i2s_config_t i2s_config = {
    //     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    //     .sample_rate = SAMPLE_RATE,
    //     .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    //     .channel_format  = I2S_CHANNEL_FMT_ONLY_LEFT,
    //     .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_MSB),
    //     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    //     .dma_buf_count = 4,
    //     .dma_buf_len = BUFFER_SIZE / 4,  // 缓冲区长度（可调整）
    //     .use_apll = false,
    //     .tx_desc_auto_clear = false,
    //     .fixed_mclk = 0
    // };

    // 引脚配置
    pin_config_ = {
        .bck_io_num = MIC_I2S_BCLK,
        .ws_io_num = MIC_II2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_II2S_DATA};

    // 初始化 I2S
    esp_err_t err = i2s_driver_install(I2S_NUM, &i2s_config_, 0, nullptr);
    if (err != ESP_OK)
        return false;

    err = i2s_set_pin(I2S_NUM, &pin_config_);
    _initialized = (err == ESP_OK);
    return _initialized;
}

// size_t Mic::Read(int32_t* buffer, size_t samples_count) {
size_t Mic::Read(int16_t *buffer, size_t samples_count)
{
    if (!_initialized)
        return 0;

    size_t bytes_read = 0;
    i2s_read(I2S_NUM,
             buffer,
             samples_count * sizeof(int16_t),
             &bytes_read,
             portMAX_DELAY);
    return bytes_read / sizeof(int16_t); // 返回实际读取的样本数
    // i2s_read(I2S_NUM,
    //         buffer,
    //         samples_count * sizeof(int32_t),
    //         &bytes_read,
    //         portMAX_DELAY);
    // return bytes_read / sizeof(int32_t);  // 返回实际读取的样本数
}

void Mic::End()
{
    if (_initialized)
    {
        i2s_driver_uninstall(I2S_NUM);
        _initialized = false;
    }
}

bool Mic::Reset()
{
    _initialized = false;
    i2s_driver_uninstall(I2S_NUM);
    i2s_zero_dma_buffer(I2S_NUM);
    delay(20);
    esp_err_t err = i2s_driver_install(I2S_NUM, &i2s_config_, 0, NULL);
    if (err != ESP_OK)
        return false;
    err = i2s_set_pin(I2S_NUM, &pin_config_);
    _initialized = (err == ESP_OK);
    return _initialized;
}