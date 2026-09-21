#ifndef NODE_H
#define NODE_H

#include <vector>
#include <unordered_map>
#include <asio.hpp>
#include <chrono>
#include <fstream>
#include <filesystem>
#include "rpc.h"
#include "kv.h"

namespace fs = std::filesystem;

enum Role {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

class CraftyNode {
    public:
        CraftyNode(int port, asio::io_context& io, KVStore store, std::string cfg_filename);

    private:
        void reset_heartbeat();
        void reset_election_timer();
        void do_heartbeat();
        void replicate_log(int peer);
        void start_election();
        void process_entries(int prefix_length, int commit_index, std::vector<LogEntry> suffix);
        void check_election();
        void become_leader();
        void step_down(int term);
        void commit();
        void persist();
        void revive();
        bool configure(std::string cfg_filename);
        std::chrono::milliseconds new_election_timeout();

        const int port_;
        int current_term_ = 0;
        int voted_for_ = 0;
        std::vector<LogEntry> log_;
        int commit_index_ = -1;
        int current_leader_ = 0;
        Role role_ = Role::FOLLOWER;
        asio::io_context& io_;
        std::vector<int> rpc_peers_;
        std::vector<int> votes_received_;
        std::unordered_map<int, std::function<void(KVReply)>> pending_;
        std::unordered_map<int, int> next_index_;
        std::unordered_map<int, int> match_index_;
        std::shared_ptr<asio::steady_timer> election_timer_;
        std::shared_ptr<asio::steady_timer> heartbeat_timer_;
        KVStore store_;
        KVService kv_service_;
        RPCService rpc_service_;
};

#endif