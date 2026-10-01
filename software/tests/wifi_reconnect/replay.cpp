#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

enum wl_status_t { WL_IDLE_STATUS, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED,
                   WL_CONNECTED, WL_CONNECT_FAILED, WL_CONNECTION_LOST,
                   WL_DISCONNECTED, WL_NO_SHIELD };
enum { WIFI_OFF, WIFI_STA, ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
       WIFI_REASON_ASSOC_LEAVE = 8 };
using WiFiEvent_t = int;
struct WiFiEventInfo_t { struct { uint8_t reason; } wifi_sta_disconnected; };
using wifi_err_reason_t = int;

class String : public std::string {
public:
    using std::string::string;
    void trim() {
        const auto first = find_first_not_of(" \t\r\n");
        if (first == npos) { clear(); return; }
        erase(find_last_not_of(" \t\r\n") + 1);
        erase(0, first);
    }
    bool isEmpty() const { return empty(); }
};
static uint32_t now = 100;
uint32_t millis() { return now; }
void delay(uint32_t elapsed) { now += elapsed; }
#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
const char* wifiStatusToText(wl_status_t) { return "fake"; }
constexpr uint32_t kWifiRetryIntervalMs = 5000;
constexpr uint32_t kWifiConnectTimeoutMs = 10000;

struct FakeWiFi {
    wl_status_t observed = WL_CONNECT_FAILED;
    int beginCalls = 0, disconnectCalls = 0;
    int eventRegistrations = 0;
    std::function<void(WiFiEvent_t, WiFiEventInfo_t)> handler;
    void persistent(bool) {}
    void setAutoReconnect(bool) {}
    void setSleep(bool) {}
    bool mode(int) { return true; }
    int getMode() { return WIFI_STA; }
    void disconnect(bool, bool) { ++disconnectCalls; }
    wl_status_t status() { return observed; }
    wl_status_t begin(const char*, const char*) { ++beginCalls; return observed; }
    int onEvent(std::function<void(WiFiEvent_t, WiFiEventInfo_t)> callback, int) {
        handler = callback;
        ++eventRegistrations;
        return 1;
    }
    const char* disconnectReasonName(wifi_err_reason_t) { return "fake"; }
} WiFi;

struct Configuration {
    enum { WIRED_KEYBOARD_MODE, BLUETOOTH_KEYBOARD_MODE };
    struct { bool wifi_switch = true; String wifi_ssid = "test";
             String wifi_password = "test-pass"; } settings_;
};
struct MainTask {
    Configuration configuration_;
    int currentWorkMode_ = Configuration::WIRED_KEYBOARD_MODE;
    bool wifiReconnectActive_ = false, wifiWasConnected_ = false;
    uint32_t wifiConnectAttemptStartedMs_ = 0, wifiNextRetryAtMs_ = 0;
    bool wifiEventRegistered_ = false;
    size_t wifiDisconnectEventHandlerId_ = 0;
    std::atomic<uint16_t> wifiLastDisconnectReason_{0};
    std::atomic<uint32_t> wifiDisconnectEventCount_{0};
    uint32_t wifiReportedDisconnectEventCount_ = 0;
    int connections = 0;
    bool ConnectToWiFi(const String&, const String&);
    void processWiFiReconnect(uint32_t);
    void stopWiFiReconnect() { wifiReconnectActive_ = false; }
    void reconcileVoiceRuntimeState() {}
    void onWiFiConnected() { ++connections; wifiConnectAttemptStartedMs_ = 0; }
};

// FIRMWARE_METHODS

int main() {
    for (auto stale : {WL_CONNECT_FAILED, WL_NO_SSID_AVAIL, WL_NO_SHIELD}) {
        WiFi = FakeWiFi{};
        WiFi.observed = stale;
        now = 100;
        MainTask task;
        assert(task.ConnectToWiFi(task.configuration_.settings_.wifi_ssid,
                                 task.configuration_.settings_.wifi_password));
        assert(task.wifiConnectAttemptStartedMs_ != 0);
        assert(WiFi.disconnectCalls == 1); // only the pre-connect cleanup
        const auto started = now;
        now = started + 4000;
        task.processWiFiReconnect(now);
        assert(WiFi.disconnectCalls == 1); // stale status must not cancel the scan
        WiFi.observed = WL_CONNECTED;
        now = started + 4500;
        task.processWiFiReconnect(now);
        assert(task.connections == 1);
        assert(task.wifiWasConnected_);
    }

    WiFi = FakeWiFi{};
    now = 100;
    MainTask retry;
    assert(retry.ConnectToWiFi(retry.configuration_.settings_.wifi_ssid,
                              retry.configuration_.settings_.wifi_password));
    WiFiEventInfo_t failure{};
    failure.wifi_sta_disconnected.reason = 202;
    WiFi.handler(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, failure);
    assert(retry.wifiLastDisconnectReason_.load() == 202);
    failure.wifi_sta_disconnected.reason = WIFI_REASON_ASSOC_LEAVE;
    WiFi.handler(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, failure);
    assert(retry.wifiLastDisconnectReason_.load() == 202);
    assert(retry.wifiDisconnectEventCount_.load() == 1);
    now += kWifiConnectTimeoutMs;
    retry.processWiFiReconnect(now);
    assert(retry.wifiConnectAttemptStartedMs_ == 0);
    assert(WiFi.disconnectCalls == 2);
    assert(retry.wifiReportedDisconnectEventCount_ == 1);
    now += kWifiRetryIntervalMs - 1;
    retry.processWiFiReconnect(now);
    assert(WiFi.beginCalls == 1);
    ++now;
    retry.processWiFiReconnect(now);
    assert(WiFi.beginCalls == 2);
    assert(WiFi.eventRegistrations == 1);
    WiFi.observed = WL_CONNECTED;
    now += 1000;
    retry.processWiFiReconnect(now);
    assert(retry.connections == 1);

    WiFi = FakeWiFi{};
    MainTask disabled;
    disabled.configuration_.settings_.wifi_switch = false;
    disabled.wifiReconnectActive_ = true;
    disabled.processWiFiReconnect(now);
    assert(WiFi.beginCalls == 0);
    MainTask ble;
    ble.currentWorkMode_ = Configuration::BLUETOOTH_KEYBOARD_MODE;
    ble.wifiReconnectActive_ = true;
    ble.processWiFiReconnect(now);
    assert(WiFi.beginCalls == 0);
    std::puts("WiFi reconnect replay OK");
}
