#pragma once
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct TestSerial {
    std::deque<unsigned char> incoming;
    std::vector<std::string> outgoing;
    int available() const { return static_cast<int>(incoming.size()); }
    int availableForWrite() const { return 4096; }
    int read() {
        if (incoming.empty()) return -1;
        const auto ch = incoming.front();
        incoming.pop_front();
        return ch;
    }
    void println(const char* line) { outgoing.emplace_back(line); }
    void feed(const std::string& line) {
        for (unsigned char ch : line + "\n") incoming.push_back(ch);
    }
};
extern TestSerial Serial;
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
