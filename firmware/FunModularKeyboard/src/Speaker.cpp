#include "Speaker.h"


Speaker::Speaker() {
   _audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
}

Speaker::~Speaker() {}

void Speaker::SetVolume(uint8_t vol) {
    //_audio.setBufsize(10240, 0);  // 若播放卡顿,增加缓冲区大小,默认是1024*5
    _audio.setVolume(vol); // 0-21
}

void Speaker::PlayRemoteAudio(String path) {
   // _audio.connecttohost(path.c_str());//("http://mp3.ffh.de/radioffh/hqlivestream.mp3"); // 替换为你的MP3链接
}

void Speaker::PlayLocalAudio(String path) {

    // 检查文件是否存在
    const char* audioFile = path.c_str(); 
    if (!SPIFFS.exists(audioFile)) {
        LOG_DEBUG("Log","File %s not found!", audioFile);
        return;
    }

    // 从SPIFFS播放文件
    LOG_DEBUG("Log","Playing: %s", audioFile);
    _audio.connecttoFS(SPIFFS, audioFile);
     LOG_DEBUG("Log","getTotalPlayingTime: %d", _audio.getTotalPlayingTime());
}

bool Speaker::TogglePauseResume() {
    return _audio.pauseResume();
}

void Speaker::Stop() {
    _audio.stopSong();
}

bool Speaker::IsRunning() {
    return _audio.isRunning();
}

uint32_t Speaker::GetCurrentTime() {
    return _audio.getAudioCurrentTime();
}

uint32_t Speaker::GetTotalPlayingTime() {
    return _audio.getTotalPlayingTime();
}

void Speaker::Loop() {
  _audio.loop(); // 必须调用以维持播放
}