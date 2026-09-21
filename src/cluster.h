#ifndef CLUSTER_H
#define CLUSTER_H

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <filesystem>

struct ClusterConfig {
    int kv_offset;
    std::vector<int> rpc_peers;
};

class CraftyCluster {
    public:
        CraftyCluster(std::string cfg_filename);
        bool configure();
        ClusterConfig get_config() { return config_; };
    private:
        bool validate();
        std::string cfg_filename_;
        ClusterConfig config_;
        bool rpc_peers_filled_ = false;
        bool kv_offset_filled_ = false;
};

#endif