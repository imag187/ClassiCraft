#include "connection.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace game {
namespace {
namespace fs = std::filesystem;
constexpr size_t maxMessage = 1024 * 1024;
struct CloseInternet {
    void operator()(void* handle) const { WinHttpCloseHandle(handle); }
};
using Internet = std::unique_ptr<void, CloseInternet>;

std::wstring Endpoint() {
    std::array<wchar_t, 2048> value{};
    const auto size = GetEnvironmentVariableW(L"CLASSICRAFT_WS_URL", value.data(), static_cast<DWORD>(value.size()));
    if (size >= value.size()) throw std::runtime_error("The world address is too long.");
    return size ? value.data() : L"ws://127.0.0.1:2570";
}

Internet OpenWorld(HINTERNET session) {
    auto url = Endpoint();
    if (url.starts_with(L"wss://")) url.replace(0, 3, L"https");
    else if (url.starts_with(L"ws://")) url.replace(0, 2, L"http");
    else throw std::runtime_error("Use a ws:// or wss:// world address.");

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
        throw std::runtime_error("The world address is invalid.");
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
    if (!connection) throw std::runtime_error("Could not open the world connection.");
    Internet request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    if (!request || !WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
        throw std::runtime_error("Could not reach the world server (" + std::to_string(GetLastError()) + ").");
    Internet socket(WinHttpWebSocketCompleteUpgrade(request.get(), 0));
    if (!socket) throw std::runtime_error("The server refused the world connection.");
    return socket;
}

bool ValidVersion(const std::string& version) {
    return version.size() == 64 && std::ranges::all_of(version, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

fs::path CachePath(const std::string& version) {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    const auto folder = fs::path(local) / L"ClassiCraft" / L"world-cache";
    CoTaskMemFree(local);
    return folder / (version + ".json");
}

std::string Sha256(const std::string& content) {
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    std::array<UCHAR, 32> digest{};
    const auto result = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(content.data())), static_cast<ULONG>(content.size()),
        digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string hash;
    for (const auto byte : digest) {
        hash += hex[byte >> 4];
        hash += hex[byte & 15];
    }
    return hash;
}
}

Connection::Connection() : worker_([this](std::stop_token stop) { Run(stop); }) {}

void Connection::Send(const Json& message) {
    std::lock_guard lock(mutex_);
    if (outgoing_.size() < 64) outgoing_.push_back(message.dump());
}

std::vector<Json> Connection::Poll() {
    std::lock_guard lock(mutex_);
    return std::exchange(incoming_, {});
}

void Connection::Publish(Json message) {
    std::lock_guard lock(mutex_);
    if (message.value("type", "") == "state" && !incoming_.empty() &&
        incoming_.back().value("type", "") == "state") {
        incoming_.back() = std::move(message);
    } else {
        incoming_.push_back(std::move(message));
    }
}

void Connection::Run(std::stop_token stop) {
    try {
        Internet session(WinHttpOpen(L"ClassiCraft", WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) throw std::runtime_error("Windows networking could not start.");
        WinHttpSetTimeouts(session.get(), 2500, 2500, 1500, 1500);
        auto socket = OpenWorld(session.get());
        DWORD closeTimeout = 500;
        WinHttpSetOption(socket.get(), WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &closeTimeout, sizeof(closeTimeout));

        std::array<char, 8192> buffer{};
        std::string packet;
        while (!stop.stop_requested()) {
            std::vector<std::string> outgoing;
            {
                std::lock_guard lock(mutex_);
                outgoing.swap(outgoing_);
            }
            for (auto& message : outgoing) {
                if (WinHttpWebSocketSend(socket.get(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                    message.data(), static_cast<DWORD>(message.size())) != NO_ERROR)
                    throw std::runtime_error("The world server stopped receiving input.");
            }
            DWORD size = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const auto result = WinHttpWebSocketReceive(socket.get(), buffer.data(),
                static_cast<DWORD>(buffer.size()), &size, &type);
            if (result != NO_ERROR) throw std::runtime_error("The world connection was interrupted (" + std::to_string(result) + ").");
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
            if (type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE &&
                type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) break;
            if (packet.size() + size > maxMessage) throw std::runtime_error("The world download is too large.");
            packet.append(buffer.data(), size);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                auto message = Json::parse(packet, nullptr, false);
                if (message.is_object()) Publish(std::move(message));
                packet.clear();
            }
        }
        WinHttpWebSocketClose(socket.get(), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        if (!stop.stop_requested()) Publish({{"type", "connection_error"}, {"message", "Disconnected from the world."}});
    } catch (const std::exception& error) {
        Publish({{"type", "connection_error"}, {"message", error.what()}});
    }
}

Json DecodeWorldContent(const std::string& version, const std::string& content) {
    if (!ValidVersion(version) || content.size() > maxMessage || Sha256(content) != version) return {};
    auto data = Json::parse(content, nullptr, false);
    if (!data.is_object() || !data.contains("tiles") || !data["tiles"].is_array() ||
        !data.contains("cells") || !data["cells"].is_array()) return {};
    return data;
}

Json ReadWorldCache(const std::string& version) {
    if (!ValidVersion(version)) return {};
    const auto path = CachePath(version);
    if (path.empty()) return {};
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size > maxMessage) return {};
    std::ifstream input(path, std::ios::binary);
    std::string content(static_cast<size_t>(size), '\0');
    if (!input.read(content.data(), static_cast<std::streamsize>(size))) return {};
    return DecodeWorldContent(version, content);
}

void SaveWorldCache(const std::string& version, const std::string& content) {
    if (!ValidVersion(version)) return;
    const auto path = CachePath(version);
    if (path.empty()) return;
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) return;
    auto temporary = path;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (output && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return;
    fs::remove(temporary, error);
}
}
