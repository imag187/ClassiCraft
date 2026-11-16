#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

namespace game {
using Json = nlohmann::json;

class Connection {
public:
    Connection();
    void Send(const Json& message);
    std::vector<Json> Poll();

private:
    void Run(std::stop_token stop);
    void Publish(Json message);

    std::mutex mutex_;
    std::vector<std::string> outgoing_;
    std::vector<Json> incoming_;
    std::jthread worker_;
};

Json ReadWorldCache(const std::string& version);
Json DecodeWorldContent(const std::string& version, const std::string& content);
void SaveWorldCache(const std::string& version, const std::string& content);
}
