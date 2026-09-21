#include "client.h"

CraftyClient::CraftyClient(asio::io_context& io, std::string cfg_filename) : io_(io) {
    configure(cfg_filename);
}

GetResult CraftyClient::get(std::string key) {
    proto::kv::KVRequest req_buf;
    proto::kv::KVReply reply_buf;
    GetResult res;
    req_buf.set_key(key);
    req_buf.set_type(proto::kv::KVRequest_RequestType_GET);
    reply_buf = do_send(req_buf, 0);
    if(reply_buf.success()) {
        res.success = true;
        res.value = reply_buf.value(); 
    } else {
        res.success = false;
    }
    return res;
}

bool CraftyClient::put(std::string key, std::string value) {
    proto::kv::KVRequest req_buf;
    proto::kv::KVReply reply_buf;
    req_buf.set_key(key);
    req_buf.set_value(value);
    req_buf.set_type(proto::kv::KVRequest_RequestType_PUT);
    reply_buf = do_send(req_buf, 0);
    return reply_buf.success();
}

bool CraftyClient::del(std::string key) {
    proto::kv::KVRequest req_buf;
    proto::kv::KVReply reply_buf;
    req_buf.set_key(key);
    req_buf.set_type(proto::kv::KVRequest_RequestType_DEL);
    reply_buf = do_send(req_buf, 0);
    return reply_buf.success();
}

proto::kv::KVReply CraftyClient::do_send(proto::kv::KVRequest req_buf, int tries) {
    std::string req_data;
    req_data = req_buf.SerializeAsString();

    try {
        asio::ip::tcp::resolver resolver(io_);
        auto endpoints = resolver.resolve("localhost", std::to_string(nodes_[leader_index_]));

        asio::ip::tcp::socket socket(io_);
        asio::connect(socket, endpoints);

        asio::write(socket, asio::buffer(req_data));

        asio::error_code error;
        std::array<char, 4096> buf;
        while (true) {
            size_t len = socket.read_some(asio::buffer(buf), error);
            if (error == asio::error::eof)
                break;
            else if (error)
                throw asio::system_error(error);
        }

        proto::kv::KVReply reply;
        reply.ParseFromString(buf.data());
        if(!reply.success() && tries < (int)nodes_.size()) {
            if(reply.leader() != nodes_[leader_index_] && reply.leader() != 0) {
                auto leader_it = std::find(nodes_.begin(), nodes_.end(), reply.leader());
                if(leader_it != nodes_.end()) {
                    leader_index_ = std::distance(nodes_.begin(), leader_it);
                } else {
                    rotate_leader();
                }
                return do_send(req_buf, tries + 1);
            } else if(reply.leader() != nodes_[leader_index_] && reply.leader() == 0) {
                rotate_leader();
                return do_send(req_buf, tries + 1);
            }

            std::cerr << "Error: key not found, sending success = false" << std::endl;
            
            // std::cerr << "Error: wrong leader. should be node " << nodes_[leader_index_] <<  " retrying" << std::endl;
        } else {
            return reply;
        }
    } catch (std::exception& e) {
        if(tries < 5) {
            // std::cerr << "Error: " << e.what() << ", retrying" << std::endl;
           rotate_leader();
            return do_send(req_buf, tries + 1);
        } else {
            std::cerr << "Error: " << e.what() << ", sending success = false" << std::endl;
        }
    }

    proto::kv::KVReply reply;
    reply.set_success(false);
    reply.set_leader(nodes_[leader_index_]);

    return reply;
}

void CraftyClient::rotate_leader() {
    if(leader_index_ != nodes_.size() - 1) {
        leader_index_++;
    } else {
        leader_index_ = 0;
    }
}

void CraftyClient::configure(std::string filename) {
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
        for (int v; in >> v; ) vals.push_back(v + 1110);

        if (key == "rpc_peers") nodes_ = vals;
    }
}

int main(int argc, char** argv) {
    asio::io_context io;
    CraftyClient client(io, "cluster.cfg");
    if(argc == 3 && std::string(argv[1]) == "get") {
        GetResult result = client.get(argv[2]);
        if(result.success) {
            std::cout << result.value << std::endl;
        }
    } else if(argc == 4 && std::string(argv[1]) == "put") {
        bool success = client.put(argv[2], argv[3]);
        if (success) std::cout << "put successful" << std::endl;
        if(!success) std::cout << "put unsucessful" << std::endl;
    } else if(argc == 3 && std::string(argv[1]) == "del") {
        bool success = client.del(argv[2]);
        if (success) std::cout << "delete successful" << std::endl;
        if(!success) std::cout << "delete unsuccessful" << std::endl;
    }
}