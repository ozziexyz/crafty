#include "cluster.h"

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <filesystem>

CraftyCluster::CraftyCluster(std::string cfg_filename) : cfg_filename_(cfg_filename) {}

bool CraftyCluster::configure() {
    std::ifstream f(cfg_filename_);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        key.erase(key.find_last_not_of(" \t") + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        std::string vals_str = line.substr(eq + 1);
        std::replace(vals_str.begin(), vals_str.end(), ',', ' ');
        std::istringstream in(vals_str);
        std::vector<int> vals;
        for (int v; in >> v; ) vals.push_back(v);
        if(key == "rpc_peers") {
            config_.rpc_peers = vals;
            rpc_peers_filled_ = true;
        } else if(key == "kv_offset") {
            config_.kv_offset = vals[0];
            kv_offset_filled_ = true;
        }
        if(kv_offset_filled_ && rpc_peers_filled_) {
            return true;
        }
    }
    return false;
}