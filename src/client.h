#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <asio.hpp>
#include <fstream>
#include "kv.pb.h"
#include "cluster.h"

using namespace crafty;

struct GetResult {
    bool success = true;
    std::string value;
};

class CraftyClient {
    public:
        CraftyClient(asio::io_context& io, ClusterConfig cfg);
        GetResult get(std::string key);
        bool put(std::string key, std::string value);
        bool del(std::string key);

    private:
        proto::kv::KVReply do_send(proto::kv::KVRequest req, int tries);
        void rotate_leader();
        
        asio::io_context& io_;
        ClusterConfig cfg_;
        std::vector<int> nodes_ = {6660, 6661, 6662, 6663, 6664};
        int leader_index_ = 0;
};

#endif