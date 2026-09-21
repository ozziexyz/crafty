#include <iostream>
#include "node.h"
#include <cmath>
#include <random>

CraftyNode::CraftyNode(int port, asio::io_context& io, KVStore store, std::string cfg_filename) : port_(port), io_(io), store_(store), rpc_service_(io, port,
    [this](AppendEntriesReply msg) {
        if(msg.current_term == current_term_ && role_ == Role::LEADER) {
            if(msg.valid && msg.ack > match_index_[msg.node_id]) {
                next_index_[msg.node_id] = msg.ack + 1;
                match_index_[msg.node_id] = msg.ack;
                commit();
            } else if(next_index_[msg.node_id] > 0 && !msg.valid) {
                next_index_[msg.node_id] -= 1;
                replicate_log(msg.node_id);
            }
        } else if(msg.current_term > current_term_) {
            step_down(msg.current_term);
            persist();
        }
    },
    [this](AppendEntries msg) {
        if(msg.current_term > current_term_) {
            step_down(msg.current_term);
            persist();
        }
        if (msg.current_term == current_term_) {
            current_leader_ = msg.leader_id;
            if(role_ == Role::LEADER) {
                step_down(msg.current_term);
            } else {
                reset_election_timer();
                role_ = Role::FOLLOWER;
            }
        }
        bool log_ok = (log_.size() >= msg.prefix_length) && (msg.prefix_length == 0 || log_[msg.prefix_length - 1].term == msg.prefix_term);
        if(msg.current_term == current_term_ && log_ok) {
            process_entries(msg.prefix_length, msg.commit_index, msg.suffix);
            int ack = msg.prefix_length + (int)msg.suffix.size() - 1;
            AppendEntriesReply reply{port_, current_term_, ack, true};
            return reply;
        } else{
            AppendEntriesReply reply{port_, current_term_, -1, false};
            return reply;
        }
    },
    [this](RequestVoteReply reply) {
        if(role_ == Role::CANDIDATE && current_term_ == reply.current_term && reply.vote) {
            votes_received_.push_back(reply.node_id);
            std::cout << "Received a vote from: " << reply.node_id << std::endl;
            check_election();
        }
    },
    [this](RequestVote msg) {
        std::cout << "Received vote request from: " << msg.node_id << std::endl;
        if(msg.current_term > current_term_) {
            step_down(msg.current_term);
            persist();
        }

        int last_index = -1;
        int last_term = 0;
        if(log_.size() > 0) {
            last_index = log_.size() - 1;
            last_term = log_[last_index].term; 
        }
        bool log_ok = (msg.last_term > last_term) || (msg.last_term == last_term && msg.last_index >= last_index);
        if(msg.current_term == current_term_ && log_ok && (voted_for_ == msg.node_id || voted_for_ == 0)) {
            RequestVoteReply reply {port_, current_term_, true};
            std::cout << "I'm voting for: " << msg.node_id << std::endl;
            voted_for_ = msg.node_id;
            reset_election_timer();
            persist();
            return reply;
        } else {
            RequestVoteReply reply {port_, current_term_, false};
            return reply;
        }
    }), kv_service_(io, port + 1110, [this](KVRequest req, std::function<void(KVReply)> respond){
        std::cout << "Received KV Request" << std::endl;
        KVReply reply;
        if (req.type == KVRequestType::GET) { 
            reply = store_.handle_request(req);
            if(current_leader_ != 0) {
                reply.leader = current_leader_ + 1110;
            } else{
                reply.leader = 0;
            }
            respond(reply); 
            return; 
        }
        if(role_ == Role::FOLLOWER) {
            std::cout << "Redirecting" << std::endl;
            if(current_leader_ != 0) {
                reply.leader = current_leader_ + 1110;
            } else {
                reply.leader = 0;
            }
            reply.success = false;
            respond(reply);
        } else if(role_ == Role::CANDIDATE) {
            std::cout << "Redirecting" << std::endl;
            reply.leader = 0;
            reply.success = false;
            respond(reply);
        } else if(role_ == Role::LEADER) {
            LogEntry entry {store_.to_buf(req), current_term_};
            log_.push_back(entry);
            match_index_[port_] = log_.size() - 1;
            int index = log_.size() - 1;
            pending_[index] = respond;
            commit();
            for(auto peer : rpc_peers_) {
                if(peer != port_) replicate_log(peer);
            }
            persist();
        }
    }) 
{
    if(configure(cfg_filename)) {
        revive();
        reset_heartbeat();
        reset_election_timer();
    }
}

void CraftyNode::reset_heartbeat() {
    heartbeat_timer_ = std::make_shared<asio::steady_timer>(io_);
    heartbeat_timer_->expires_after(std::chrono::milliseconds(10));
    heartbeat_timer_->async_wait([this](asio::error_code ec){
        if(!ec) do_heartbeat();
    });
}

void CraftyNode::reset_election_timer() {
    if(election_timer_) {
        election_timer_->cancel();   
    }
    election_timer_ = std::make_shared<asio::steady_timer>(io_);
    election_timer_->expires_after(new_election_timeout());
    election_timer_->async_wait([this](asio::error_code ec) {
        if(!ec) {
            start_election();
        }
    });
}

void CraftyNode::do_heartbeat() {
    if(role_ == Role::LEADER) {
        for(auto peer : rpc_peers_) {
            if(peer != port_) replicate_log(peer);
        }
    }
    reset_heartbeat();
}

void CraftyNode::replicate_log(int peer) {
    int prefix_length = next_index_[peer];
    int prefix_term = 0;
    std::vector<LogEntry> suffix(log_.begin() + prefix_length, log_.end());
    if(prefix_length > 0) {
        prefix_term = log_[prefix_length - 1].term;
    }
    AppendEntries msg {
        port_,
        current_term_,
        commit_index_,
        prefix_length,
        prefix_term,
        suffix
    };
    rpc_service_.append_entries(msg, peer);
}

void CraftyNode::start_election() {
    std::cout << "Election timeout" << std::endl;
    current_term_++;
    role_ = Role::CANDIDATE;
    voted_for_ = port_;
    if(votes_received_.size() > 0) votes_received_.clear();
    votes_received_.push_back(port_);
    int last_term = 0;
    if(log_.size() > 0) {
        last_term = log_[log_.size() - 1].term;
    }
    RequestVote msg{
        port_,
        current_term_,
        (int)log_.size() - 1,
        last_term
    };
    check_election();
    for(auto peer : rpc_peers_) {
        if(peer != port_) rpc_service_.request_vote(msg, peer);
    }
    if(role_ == Role::CANDIDATE) {
        reset_election_timer();
    }
    persist();
}

void CraftyNode::process_entries(int prefix_length, int leader_commit, std::vector<LogEntry> suffix) {
    int index;
    if(suffix.size() > 0 && log_.size() > prefix_length) {
        index = std::min(log_.size(), prefix_length + suffix.size()) - 1;
        if(log_[index].term != suffix[index - prefix_length].term) {
            std::vector<LogEntry> new_log_(log_.begin(), log_.begin() + prefix_length);
            log_ = new_log_;
        }
        persist();
    }
    if(prefix_length + suffix.size() > log_.size()) {
        for(int i = log_.size() - prefix_length; i <= suffix.size() - 1; i++) {
            log_.push_back(suffix[i]);
        }
        persist();
    }
    int last_new = prefix_length + (int)suffix.size() - 1;
    int new_commit = std::min(leader_commit, last_new);
    if(new_commit > commit_index_) {
        for(int i = commit_index_ + 1; i <= new_commit; i++) {
            if(log_[i].data.empty()) continue;
            store_.handle_request(store_.from_buf(log_[i].data));
        }
        commit_index_ = new_commit;
    }
}

void CraftyNode::check_election() {
    if(votes_received_.size() >= rpc_peers_.size() / 2 + 1) {
        current_leader_ = port_;
        election_timer_->cancel();
        become_leader();
    }
}

void CraftyNode::become_leader() {
    std::cout << "I'm the leader" << std::endl;
    role_ = Role::LEADER;
    LogEntry entry {"", current_term_};
    log_.push_back(entry);
    match_index_[port_] = log_.size() - 1;
    for(auto peer : rpc_peers_) {
        if(peer != port_) {
            next_index_[peer] = log_.size() - 1;
            match_index_[peer] = -1;
        }
    }
    persist();
    for(auto peer : rpc_peers_) {
        if(peer != port_) replicate_log(peer);
    }
}

void CraftyNode::step_down(int term) {
    current_term_ = term;
    role_ = Role::FOLLOWER;
    current_leader_ = 0;
    voted_for_ = 0;
    for(auto p : pending_) {
        p.second(KVReply{"", 0, false});
    }
    pending_.clear();
    reset_election_timer();
}

void CraftyNode::commit() {
    int quorum = rpc_peers_.size() / 2 + 1;
    int new_commit = commit_index_;
    for(int n = (int)log_.size() - 1; n > commit_index_; n--) {
        if(log_[n].term != current_term_) break;
        int acks = 0;
        for(auto peer : rpc_peers_) {
            if(match_index_[peer] >= n) acks++;
        }
        if(acks >= quorum) {
            new_commit = n;
            break;
        }
    }
    for(int i = commit_index_ + 1; i <= new_commit; i++) {
        if(log_[i].data.empty()) continue;
        KVReply reply = store_.handle_request(store_.from_buf(log_[i].data));
        reply.leader = port_ + 1110;
        auto it = pending_.find(i);
        if(it != pending_.end()) {
            it->second(reply);
            pending_.erase(it);
        }
    }
    commit_index_ = new_commit;
}

void CraftyNode::persist() {
    fs::path state_dir = "state";
    fs::path state_file = state_dir / ("node" + std::to_string(port_) + ".state");
    std::error_code ec;
    fs::create_directories(state_dir, ec);
    if(ec) {
        std::cerr << "Could not create directory: " << ec.message() << std::endl;
    }

    proto::rpc::PersistentState state;
    std::string state_str;
    state.set_voted_for(voted_for_);
    state.set_current_term(current_term_);

    for(auto i : log_) {
        proto::rpc::LogEntry* entry = state.add_log();
        entry->set_term(i.term);
        entry->set_data(i.data);
    }

    state_str = state.SerializeAsString();
    std::ofstream out_file(state_file);
    out_file << state_str;
    out_file.close();
}

void CraftyNode::revive() {
    std::ifstream in("state/node" + std::to_string(port_) + ".state");
    std::string state_data;
    proto::rpc::PersistentState state_buf;
    if(in.good()) {
        state_data = std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        state_buf.ParseFromString(state_data);
        voted_for_ = state_buf.voted_for();
        current_term_ = state_buf.current_term();
        for(auto i : state_buf.log()) {
            LogEntry entry;
            entry.term = i.term();
            entry.data = i.data();
            log_.push_back(entry);
        }
    }
    in.close();
}

bool CraftyNode::configure(std::string filename) {
    std::ifstream f(filename);
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

        if (key == "rpc_peers") rpc_peers_ = vals;
    }
    if(std::find(rpc_peers_.begin(), rpc_peers_.end(), port_) == rpc_peers_.end()) {
        std::cerr << "Config error: node port must be in rpc_peers" << std::endl;
        return false;
    }
    return true;
}

std::chrono::milliseconds CraftyNode::new_election_timeout() {
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(100, 250);
    return std::chrono::milliseconds(dist(rng));
}

int main(int argc, char** argv) {
    int port;
    std::string cfg_filename = "cluster.cfg";

    if(argc == 1){
        std::cout << "Error: No port specified." << std::endl;
        return EXIT_FAILURE;
    } else if(argc > 1) {
        port = atoi(argv[1]);
    }

    if(argc > 2) {
        cfg_filename = argv[2];
    }

    KVStore store;
    asio::io_context io;
    CraftyNode node(port, io, store, cfg_filename);
    io.run();

    return EXIT_FAILURE;
}