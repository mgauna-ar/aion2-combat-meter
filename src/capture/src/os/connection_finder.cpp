#include "capture/os/connection_finder.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>

#include "common/os/unique_handle.hpp"

#include <algorithm>
#include <cwctype>
#include <set>
#include <span>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace aion2::capture {

namespace {

std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring widen_ascii(const std::string& s) { return std::wstring(s.begin(), s.end()); }

/// Ids of processes named `name`, from a snapshot of the process list. A snapshot
/// lists names and ids only: no handle to any of them is opened.
std::set<DWORD> pids_named(const std::string& name) {
    std::set<DWORD> pids;
    const std::wstring wanted = lower(widen_ascii(name));
    const os::UniqueHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snap) return pids;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snap.get(), &entry); ok; ok = Process32NextW(snap.get(), &entry)) {
        if (lower(entry.szExeFile) == wanted) pids.insert(entry.th32ProcessID);
    }
    return pids;
}

/// The table GetExtendedTcpTable fills, sized by a first call.
std::vector<uint8_t> tcp_table(ULONG family) {
    std::vector<uint8_t> buf;
    DWORD size = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const DWORD rc = GetExtendedTcpTable(buf.empty() ? nullptr : buf.data(), &size, FALSE, family,
                                             TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (rc == NO_ERROR) return buf;
        if (rc != ERROR_INSUFFICIENT_BUFFER) return {};
        buf.resize(size + 1024);  // the table can grow between calls
        size = static_cast<DWORD>(buf.size());
    }
    return {};
}

bool wanted(const IpAddress& remote, uint16_t remote_port, uint16_t server_port) {
    return remote_port == server_port || remote.is_loopback();
}

} // namespace

bool game_running(const std::string& process_name) { return !pids_named(process_name).empty(); }

std::vector<uint32_t> game_process_ids(const std::string& process_name) {
    const std::set<DWORD> pids = pids_named(process_name);
    return std::vector<uint32_t>(pids.begin(), pids.end());
}

std::vector<GameConnection> find_game_connections(const std::string& process_name, uint16_t server_port) {
    std::vector<GameConnection> out;
    const std::set<DWORD> pids = pids_named(process_name);
    if (pids.empty()) return out;

    const auto v4 = tcp_table(AF_INET);
    if (v4.size() >= sizeof(MIB_TCPTABLE_OWNER_PID)) {
        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(v4.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCPROW_OWNER_PID& row = table->table[i];
            if (row.dwState != static_cast<DWORD>(MIB_TCP_STATE_ESTAB) || pids.count(row.dwOwningPid) == 0) continue;
            GameConnection c;
            c.pid = row.dwOwningPid;
            c.local = IpAddress::v4(ntohl(row.dwLocalAddr));
            c.local_port = ntohs(static_cast<u_short>(row.dwLocalPort));
            c.remote = IpAddress::v4(ntohl(row.dwRemoteAddr));
            c.remote_port = ntohs(static_cast<u_short>(row.dwRemotePort));
            if (wanted(c.remote, c.remote_port, server_port)) out.push_back(c);
        }
    }

    const auto v6 = tcp_table(AF_INET6);
    if (v6.size() >= sizeof(MIB_TCP6TABLE_OWNER_PID)) {
        const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(v6.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCP6ROW_OWNER_PID& row = table->table[i];
            if (row.dwState != static_cast<DWORD>(MIB_TCP_STATE_ESTAB) || pids.count(row.dwOwningPid) == 0) continue;
            GameConnection c;
            c.pid = row.dwOwningPid;
            c.local = IpAddress::v6(std::span<const uint8_t, 16>(row.ucLocalAddr, 16));
            c.local_port = ntohs(static_cast<u_short>(row.dwLocalPort));
            c.remote = IpAddress::v6(std::span<const uint8_t, 16>(row.ucRemoteAddr, 16));
            c.remote_port = ntohs(static_cast<u_short>(row.dwRemotePort));
            if (wanted(c.remote, c.remote_port, server_port)) out.push_back(c);
        }
    }
    return out;
}

} // namespace aion2::capture

#else // !_WIN32

namespace aion2::capture {

bool game_running(const std::string&) { return false; }

std::vector<uint32_t> game_process_ids(const std::string&) { return {}; }

std::vector<GameConnection> find_game_connections(const std::string&, uint16_t) { return {}; }

} // namespace aion2::capture

#endif
