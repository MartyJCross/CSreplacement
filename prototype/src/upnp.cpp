#include "upnp.h"
#include <enet/enet.h>
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// This PC's address on the local network: the one a UDP socket would use to reach the internet (no packet
// is sent: connecting a datagram socket only picks the route). Needs ENet initialized (hosting does that).
std::string localAddress() {
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) return "";
    ENetAddress to, me;
    std::string out;
    if (enet_address_set_host_ip(&to, "8.8.8.8") == 0) {
        to.port = 53;
        char buf[64] = {};
        if (enet_socket_connect(s, &to) == 0 && enet_socket_get_address(s, &me) == 0 &&
            enet_address_get_host_ip(&me, buf, sizeof(buf)) == 0)
            out = buf;
    }
    enet_socket_destroy(s);
    return out;
}

// Private ranges (10/8, 172.16/12, 192.168/16, and 100.64/10 carrier-grade NAT): not reachable from outside.
bool privateAddress(const std::string& ip) {
    char* end = nullptr;
    const unsigned long a = std::strtoul(ip.c_str(), &end, 10);
    if (!end || *end != '.') return false;
    const unsigned long b = std::strtoul(end + 1, nullptr, 10);
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127);
}

}  // namespace

void PortOpener::open(uint16_t port) {
    close();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = PortStatus{};
        status_.state = PortStatus::Working;
        status_.localIp = localAddress();
    }
    port_ = port;
    thread_ = std::thread([this, port] { run(port); });
}

void PortOpener::run(uint16_t port) {
    int err = 0;
    UPNPDev* devices = upnpDiscover(2000, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &err);
    UPNPUrls urls;
    IGDdatas data;
    std::memset(&urls, 0, sizeof(urls));
    std::memset(&data, 0, sizeof(data));
    char lan[64] = {}, wan[64] = {};
    const int igd = devices ? UPNP_GetValidIGD(devices, &urls, &data, lan, sizeof(lan), wan, sizeof(wan)) : 0;
    freeUPNPDevlist(devices);
    PortStatus st;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        st = status_;
    }
    if (lan[0]) st.localIp = lan;
    if (igd == 0) {
        st.state = PortStatus::Failed;
        st.note = "NO ROUTER ANSWERED (UPNP IS OFF ON IT?)";
    } else {
        const std::string p = std::to_string(port);
        // A 2-hour lease; some routers only take permanent ones (lease 0): then that, removed when you stop.
        int r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, p.c_str(), p.c_str(), lan, "Crisp", "UDP",
                                    nullptr, "7200");
        if (r != UPNPCOMMAND_SUCCESS)
            r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, p.c_str(), p.c_str(), lan, "Crisp", "UDP",
                                    nullptr, "0");
        char ext[40] = {};
        if (UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, ext) == UPNPCOMMAND_SUCCESS) st.publicIp = ext;
        if (r == UPNPCOMMAND_SUCCESS) {
            st.state = PortStatus::Opened;
            std::lock_guard<std::mutex> lock(mutex_);
            controlUrl_ = urls.controlURL;
            serviceType_ = data.first.servicetype;
            mapped_ = true;
        } else {
            st.state = PortStatus::Failed;
            st.note = std::string("THE ROUTER SAID NO (") + strupnperror(r) + ")";
        }
        if (!st.publicIp.empty() && privateAddress(st.publicIp)) {
            const bool carrier = st.publicIp.rfind("100.", 0) == 0;
            st.note = carrier ? "YOUR INTERNET PROVIDER SHARES ONE ADDRESS (CARRIER-GRADE NAT): FRIENDS NEED ZEROTIER"
                              : "YOUR ROUTER SITS BEHIND ANOTHER ONE (" + st.publicIp + "): FORWARD UDP " + std::to_string(port) +
                                    " THERE TOO, OR USE ZEROTIER";
        }
        FreeUPNPUrls(&urls);
    }
    std::fprintf(stderr, "upnp: router %d, this pc %s, router's internet address %s, port %s%s\n", igd, st.localIp.c_str(),
                 st.publicIp.empty() ? "?" : st.publicIp.c_str(), st.state == PortStatus::Opened ? "opened" : "not opened",
                 st.note.empty() ? "" : (" - " + st.note).c_str());
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = st;
}

void PortOpener::close() {
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    if (mapped_) {
        const std::string p = std::to_string(port_);
        UPNP_DeletePortMapping(controlUrl_.c_str(), serviceType_.c_str(), p.c_str(), "UDP", nullptr);
        mapped_ = false;
    }
    status_ = PortStatus{};
}

PortStatus PortOpener::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
