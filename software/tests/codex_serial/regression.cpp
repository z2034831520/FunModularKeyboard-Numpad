#include "CodexSerialBridge.h"
#include <cassert>
#include <iostream>

TestSerial Serial;
uint32_t testMillis = 1;
static int notifications = 0;
static CodexEffort lastEffort = CodexEffort::UNKNOWN;

static void statusChanged(CodexStatus, uint8_t, CodexEffort effort, void*) {
    ++notifications;
    lastEffort = effort;
}

int main() {
    CodexSerialBridge bridge;
    bridge.SetStatusCallback(statusChanged, nullptr);
    bridge.Begin();
    Serial.feed("CX<STATUS|READY|1|ULTRA");
    bridge.Loop();
    assert(bridge.CurrentEffort() == CodexEffort::EFFORT_ULTRA);
    const int before = notifications;
    Serial.feed("CX<STATUS|READY|1");
    bridge.Loop();
    assert(bridge.CurrentEffort() == CodexEffort::UNKNOWN);
    assert(notifications == before + 1);
    assert(lastEffort == CodexEffort::UNKNOWN);
    assert(Serial.outgoing.back() == "CX>STATUS|READY|1");

    Serial.feed("CX<STATUS|READY|1|HIGH");
    bridge.Loop();
    Serial.feed("CX<PING");
    bridge.Loop();
    assert(bridge.CurrentEffort() == CodexEffort::EFFORT_HIGH);
    Serial.feed("CX<STATUS|RUNNING|2");
    bridge.Loop();
    assert(bridge.CurrentStatus() == CodexStatus::RUNNING);
    assert(bridge.CurrentTaskCount() == 2);
    assert(bridge.CurrentEffort() == CodexEffort::UNKNOWN);
    assert(Serial.outgoing.back() == "CX>STATUS|RUNNING|2");

    Serial.feed("CX<STATUS|READY|1|HIGH");
    bridge.Loop();
    testMillis += 6501;
    bridge.Loop();
    assert(bridge.CurrentStatus() == CodexStatus::DISCONNECTED);
    assert(bridge.CurrentEffort() == CodexEffort::UNKNOWN);
    std::cout << "Codex serial effort regression OK\n";
}
