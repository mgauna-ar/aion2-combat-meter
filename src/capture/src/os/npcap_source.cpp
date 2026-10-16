#include "capture/os/npcap_source.hpp"

#include <algorithm>
#include <string>

namespace aion2::capture {

std::string NpcapSource::adapter_filter(uint16_t server_port) {
    return "tcp and src port " + std::to_string(server_port);
}

std::string NpcapSource::loopback_filter(uint16_t server_port, const std::vector<uint16_t>& relay_ports) {
    std::vector<uint16_t> ports{server_port};
    for (const uint16_t p : relay_ports) {
        if (std::find(ports.begin(), ports.end(), p) == ports.end()) ports.push_back(p);
    }
    std::string filter = "tcp and (";
    for (size_t i = 0; i < ports.size(); ++i) {
        if (i > 0) filter += " or ";
        filter += "src port " + std::to_string(ports[i]);
    }
    return filter + ")";
}

} // namespace aion2::capture

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "capture/os/connection_finder.hpp"
#include "common/os/logger.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace aion2::capture {

namespace {

// The few libpcap types the meter touches, declared here so nothing links the
// Npcap SDK. Layouts follow pcap/pcap.h as Npcap builds it for 64-bit Windows.
struct PcapHandle;  // pcap_t, opaque
struct PcapSockaddr {
    uint16_t family;
    char data[14];
};
struct PcapAddr {
    PcapAddr* next;
    PcapSockaddr* addr;
    PcapSockaddr* netmask;
    PcapSockaddr* broadaddr;
    PcapSockaddr* dstaddr;
};
struct PcapIf {
    PcapIf* next;
    char* name;
    char* description;
    PcapAddr* addresses;
    uint32_t flags;
};
struct BpfProgram {
    unsigned int bf_len;
    void* bf_insns;
};
/// struct timeval on Windows is two 32-bit longs.
struct PcapPkthdr {
    long tv_sec;
    long tv_usec;
    uint32_t caplen;
    uint32_t len;
};
/// pcap_stat with room for the Windows-only fields after the three common ones.
struct PcapStat {
    unsigned int ps_recv;
    unsigned int ps_drop;
    unsigned int ps_ifdrop;
    unsigned int windows_extra[8];
};

constexpr size_t kErrbufSize = 256;
constexpr uint32_t kIfLoopback = 0x00000001;
constexpr uint32_t kIfUp = 0x00000002;
constexpr uint32_t kNetmaskUnknown = 0xFFFFFFFF;
constexpr int kSnaplen = 65535;
constexpr int kReadTimeoutMs = 100;

/// Read-only wpcap.dll entry points. There is deliberately no send function here.
struct NpcapApi {
    HMODULE module{nullptr};
    int (*findalldevs)(PcapIf**, char*){nullptr};
    void (*freealldevs)(PcapIf*){nullptr};
    PcapHandle* (*create)(const char*, char*){nullptr};
    int (*set_snaplen)(PcapHandle*, int){nullptr};
    int (*set_promisc)(PcapHandle*, int){nullptr};
    int (*set_timeout)(PcapHandle*, int){nullptr};
    int (*set_immediate_mode)(PcapHandle*, int){nullptr};
    int (*set_buffer_size)(PcapHandle*, int){nullptr};
    int (*activate)(PcapHandle*){nullptr};
    int (*compile)(PcapHandle*, BpfProgram*, const char*, int, uint32_t){nullptr};
    int (*setfilter)(PcapHandle*, BpfProgram*){nullptr};
    void (*freecode)(BpfProgram*){nullptr};
    int (*next_ex)(PcapHandle*, PcapPkthdr**, const unsigned char**){nullptr};
    int (*datalink)(PcapHandle*){nullptr};
    int (*stats)(PcapHandle*, PcapStat*){nullptr};
    void (*close)(PcapHandle*){nullptr};
    char* (*geterr)(PcapHandle*){nullptr};
    const char* (*lib_version)(){nullptr};

    ~NpcapApi() {
        if (module != nullptr) FreeLibrary(module);
    }

    template <typename Fn>
    bool resolve(Fn& fn, const char* name) {
        fn = reinterpret_cast<Fn>(GetProcAddress(module, name));
        return fn != nullptr;
    }

    bool load(std::string& error) {
        wchar_t system_dir[MAX_PATH];
        const UINT len = GetSystemDirectoryW(system_dir, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            error = "cannot find the System32 folder";
            return false;
        }
        const std::wstring path = std::wstring(system_dir, len) + L"\\Npcap\\wpcap.dll";
        // From Npcap's own folder, with its Packet.dll beside it: never a wpcap.dll
        // that happens to sit on the search path.
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr) {
            error = "Npcap is not installed: get it from npcap.com, with \"Support loopback traffic\" ticked";
            return false;
        }
        const bool ok = resolve(findalldevs, "pcap_findalldevs") && resolve(freealldevs, "pcap_freealldevs") &&
                        resolve(create, "pcap_create") && resolve(set_snaplen, "pcap_set_snaplen") &&
                        resolve(set_promisc, "pcap_set_promisc") && resolve(set_timeout, "pcap_set_timeout") &&
                        resolve(set_immediate_mode, "pcap_set_immediate_mode") &&
                        resolve(set_buffer_size, "pcap_set_buffer_size") && resolve(activate, "pcap_activate") &&
                        resolve(compile, "pcap_compile") && resolve(setfilter, "pcap_setfilter") &&
                        resolve(freecode, "pcap_freecode") && resolve(next_ex, "pcap_next_ex") &&
                        resolve(datalink, "pcap_datalink") && resolve(stats, "pcap_stats") &&
                        resolve(close, "pcap_close") && resolve(geterr, "pcap_geterr") &&
                        resolve(lib_version, "pcap_lib_version");
        if (!ok) error = "this Npcap is too old: install the current one from npcap.com";
        return ok;
    }
};

bool is_loopback_device(const PcapIf& dev) {
    if ((dev.flags & kIfLoopback) != 0) return true;
    const std::string name = dev.name != nullptr ? dev.name : "";
    return name.find("NPF_Loopback") != std::string::npos;
}

} // namespace

struct NpcapSource::Impl {
    struct Device {
        std::string name;
        std::string description;
        bool loopback{false};
        PcapHandle* handle{nullptr};
        uint32_t link{0};
        std::thread thread;
        /// The filter generation this device has applied.
        uint64_t applied{0};
        std::atomic<uint64_t> dropped{0};
    };

    NpcapOptions options;
    NpcapApi api;
    Push push;
    std::vector<std::unique_ptr<Device>> devices;
    std::thread watcher;
    std::atomic<bool> stopping{false};

    mutable std::mutex mutex;
    std::string loopback_filter;
    std::atomic<uint64_t> filter_generation{1};
    std::string status;

    explicit Impl(NpcapOptions o) : options(std::move(o)) {}

    void set_status(std::string s) {
        std::lock_guard<std::mutex> lock(mutex);
        if (s != status) os::Logger::info("capture: " + s);
        status = std::move(s);
    }

    bool apply_filter(Device& d, const std::string& filter) {
        BpfProgram program{};
        if (api.compile(d.handle, &program, filter.c_str(), 1, kNetmaskUnknown) != 0) {
            os::Logger::warn("capture: cannot compile filter on " + d.description + ": " + api.geterr(d.handle));
            return false;
        }
        const int rc = api.setfilter(d.handle, &program);
        api.freecode(&program);
        if (rc != 0) os::Logger::warn("capture: cannot set filter on " + d.description + ": " + api.geterr(d.handle));
        return rc == 0;
    }

    void capture_loop(Device& d) {
        PcapStat st{};
        uint64_t reads_since_stats = 0;
        while (!stopping.load()) {
            if (d.loopback && d.applied != filter_generation.load()) {
                std::string filter;
                uint64_t generation = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    filter = loopback_filter;
                    generation = filter_generation.load();
                }
                apply_filter(d, filter);
                d.applied = generation;
            }

            PcapPkthdr* header = nullptr;
            const unsigned char* data = nullptr;
            const int rc = api.next_ex(d.handle, &header, &data);
            if (rc == 1 && header != nullptr && data != nullptr) {
                OwnedPacket pkt;
                pkt.link_type = d.link;
                pkt.ts = static_cast<Micros>(header->tv_sec) * kMicrosPerSecond + static_cast<Micros>(header->tv_usec);
                pkt.data.assign(data, data + header->caplen);
                push(std::move(pkt));
            } else if (rc < 0) {
                os::Logger::warn("capture: " + d.description + " stopped: " + api.geterr(d.handle));
                return;
            }
            // Driver drops, read on this thread since a handle is not shared.
            if (++reads_since_stats >= 200 || rc == 0) {
                reads_since_stats = 0;
                if (api.stats(d.handle, &st) == 0) d.dropped = st.ps_drop;
            }
        }
    }

    void watch_loop() {
        std::vector<uint16_t> relays;
        bool first = true;
        while (!stopping.load()) {
            const auto conns = find_game_connections(options.game_process, options.server_port);
            std::vector<uint16_t> now_relays;
            bool direct = false;
            for (const auto& c : conns) {
                if (c.relayed()) now_relays.push_back(c.remote_port);
                else direct = true;
            }
            std::sort(now_relays.begin(), now_relays.end());
            now_relays.erase(std::unique(now_relays.begin(), now_relays.end()), now_relays.end());
            if (first || now_relays != relays) {
                relays = now_relays;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    loopback_filter = NpcapSource::loopback_filter(options.server_port, relays);
                }
                ++filter_generation;
                first = false;
            }

            const size_t open = devices.size();
            std::string s = "capturing on " + std::to_string(open) + " adapter" + (open == 1 ? "" : "s");
            if (!relays.empty()) s += "; game connected through a local relay";
            else if (direct) s += "; game connected";
            else if (game_running(options.game_process)) s += "; " + options.game_process + " is running, not in game yet";
            else s += "; waiting for " + options.game_process;
            set_status(std::move(s));

            for (int waited = 0; waited < kWatchIntervalMs && !stopping.load(); waited += 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    bool open_devices(std::string& error) {
        char errbuf[kErrbufSize] = {};
        PcapIf* all = nullptr;
        if (api.findalldevs(&all, errbuf) != 0) {
            error = std::string("cannot list adapters: ") + errbuf;
            return false;
        }
        std::string first_error;
        for (PcapIf* dev = all; dev != nullptr; dev = dev->next) {
            const bool loopback = is_loopback_device(*dev);
            if (!loopback && ((dev->flags & kIfUp) == 0 || dev->addresses == nullptr)) continue;

            auto d = std::make_unique<Device>();
            d->name = dev->name != nullptr ? dev->name : "";
            d->description = dev->description != nullptr ? dev->description : d->name;
            d->loopback = loopback;
            d->handle = api.create(dev->name, errbuf);
            if (d->handle == nullptr) continue;
            api.set_snaplen(d->handle, kSnaplen);
            api.set_promisc(d->handle, 0);  // only this PC's own traffic
            api.set_timeout(d->handle, kReadTimeoutMs);
            api.set_immediate_mode(d->handle, 1);
            api.set_buffer_size(d->handle, options.buffer_bytes);
            if (api.activate(d->handle) < 0) {
                if (first_error.empty()) first_error = d->description + ": " + api.geterr(d->handle);
                api.close(d->handle);
                continue;
            }
            d->link = static_cast<uint32_t>(api.datalink(d->handle));
            if (!loopback) apply_filter(*d, NpcapSource::adapter_filter(options.server_port));
            os::Logger::info("capture: opened " + d->description + (loopback ? " (loopback)" : ""));
            devices.push_back(std::move(d));
        }
        api.freealldevs(all);
        if (devices.empty()) {
            error = first_error.empty() ? "no network adapter to capture on"
                                        : "cannot open any adapter (" + first_error +
                                              "); if Npcap was installed for administrators only, run the meter as "
                                              "administrator or reinstall Npcap without that option";
            return false;
        }
        return true;
    }
};

NpcapSource::NpcapSource(NpcapOptions options) : m_impl(std::make_unique<Impl>(std::move(options))) {}

NpcapSource::~NpcapSource() { stop(); }

bool NpcapSource::start(Push push, std::string& error) {
    Impl& impl = *m_impl;
    if (!impl.api.load(error)) {
        impl.set_status(error);
        return false;
    }
    os::Logger::info(std::string("capture: ") + impl.api.lib_version());
    impl.push = std::move(push);
    if (!impl.open_devices(error)) {
        impl.set_status(error);
        return false;
    }
    impl.stopping = false;
    {
        // Set before any thread runs: an empty filter would let everything through.
        std::lock_guard<std::mutex> lock(impl.mutex);
        impl.loopback_filter = loopback_filter(impl.options.server_port, {});
    }
    for (auto& d : impl.devices) {
        Impl::Device* dev = d.get();
        dev->thread = std::thread([&impl, dev] { impl.capture_loop(*dev); });
    }
    impl.watcher = std::thread([&impl] { impl.watch_loop(); });
    return true;
}

void NpcapSource::stop() {
    if (!m_impl) return;
    Impl& impl = *m_impl;
    impl.stopping = true;
    if (impl.watcher.joinable()) impl.watcher.join();
    for (auto& d : impl.devices) {
        if (d->thread.joinable()) d->thread.join();
    }
    for (auto& d : impl.devices) {
        if (d->handle != nullptr) impl.api.close(d->handle);
        d->handle = nullptr;
    }
    impl.devices.clear();
}

uint64_t NpcapSource::dropped() const {
    uint64_t total = 0;
    for (const auto& d : m_impl->devices) total += d->dropped.load();
    return total;
}

std::string NpcapSource::status() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->status;
}

} // namespace aion2::capture

#else // !_WIN32

namespace aion2::capture {

struct NpcapSource::Impl {
    NpcapOptions options;
};

NpcapSource::NpcapSource(NpcapOptions options) : m_impl(std::make_unique<Impl>(Impl{std::move(options)})) {}

NpcapSource::~NpcapSource() = default;

bool NpcapSource::start(Push, std::string& error) {
    error = "live capture runs on Windows only; replay a capture file instead";
    return false;
}

void NpcapSource::stop() {}

uint64_t NpcapSource::dropped() const { return 0; }

std::string NpcapSource::status() const { return "live capture runs on Windows only"; }

} // namespace aion2::capture

#endif
