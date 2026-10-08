// Hosting online: asks the home router to open the game's UDP port (UPnP), and works out the addresses
// to give friends. Runs on a background thread (finding the router can take a couple of seconds), so the
// game never waits on it.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct PortStatus {
    enum State { Idle, Working, Opened, Failed } state = Idle;
    std::string publicIp;  // what friends over the internet use (empty if unknown)
    std::string localIp;   // what people in the same house use
    std::string note;      // why it didn't work, or a warning
};

class PortOpener {
public:
    ~PortOpener() { close(); }
    void open(uint16_t port);  // starts trying in the background
    void close();              // removes the mapping it made (if any); waits for the thread
    PortStatus status() const;

private:
    void run(uint16_t port);
    mutable std::mutex mutex_;
    PortStatus status_;
    std::thread thread_;
    uint16_t port_ = 0;
    std::string controlUrl_, serviceType_;  // to remove the mapping again
    bool mapped_ = false;
};
