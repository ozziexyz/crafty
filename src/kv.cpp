#include "kv.h"

KVSession::KVSession(
    tcp::socket socket, 
    std::function<void(std::string, std::function<void(std::string)>)> read_callback
) : socket_(std::move(socket)), read_callback_(read_callback) {}

void KVSession::start() {
    do_read();
}

void KVSession::do_read() {
    auto self(shared_from_this());
    asio::async_read(socket_, asio::buffer(header_),
        [this, self](std::error_code ec, std::size_t) {
            if (ec) { std::cout << "Read error: " << ec.message() << std::endl; return;};
            uint32_t len = decode_length(header_);
            if (len > MAX_FRAME_SIZE) { std::cout << "Frame too large: " << len << std::endl; return; }
            data_in_.resize(len);
            asio::async_read(socket_, asio::buffer(data_in_),
            [this, self](std::error_code ec, std::size_t) {
                if (ec) { std::cout << "Read error: " << ec.message() << std::endl; return; }
                read_callback_(data_in_, [this, self](std::string reply_bytes){
                    data_out_ = encode_frame(reply_bytes);
                    do_write();
                });                
            });
        });
}

void KVSession::do_write() {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(data_out_),
        [this, self](std::error_code ec, std::size_t) {
            if (ec) { std::cout << "Write error: " << ec.message() << std::endl; return;};
        });
}

KVService::KVService(
    asio::io_context& io, 
    int port,
    std::function<void(KVRequest, std::function<void(KVReply)>)> request_callback
) : port_(port), io_(io), acceptor_(io, tcp::endpoint(tcp::v4(), port)), request_callback_(request_callback) {
    do_accept();
}

void KVService::do_accept() {
    acceptor_.async_accept(
        [this](std::error_code ec, tcp::socket socket) {
            if(ec) {
                std::cout << "Accept error: " << ec.message() << std::endl;
            } else {
                std::make_shared<KVSession>(std::move(socket), [this](std::string data, std::function<void(std::string)> respond){
                    proto::kv::KVRequest req_buf;
                    if(!req_buf.ParseFromString(data)) {
                        std::cout << "Error: malformed request" << std::endl;
                        proto::kv::KVReply reply_buf;
                        reply_buf.set_success(false);
                        reply_buf.set_leader(0);
                        std::string reply_str;
                        reply_str = reply_buf.SerializeAsString();
                        respond(reply_str);
                    } else {
                        KVRequest req;
                        switch(req_buf.type()) {
                            case proto::kv::KVRequest_RequestType_GET:
                                req.type = KVRequestType::GET;
                                break;
                            case proto::kv::KVRequest_RequestType_PUT:
                                req.type = KVRequestType::PUT;
                                break;
                            case proto::kv::KVRequest_RequestType_DEL:
                                req.type = KVRequestType::DEL;
                                break;
                        }
                        req.key = req_buf.key();
                        req.value = req_buf.value();
                        request_callback_(req, [respond](KVReply reply){
                            proto::kv::KVReply reply_buf;
                            reply_buf.set_success(reply.success);
                            reply_buf.set_leader(reply.leader);
                            reply_buf.set_value(reply.value);
                            std::string reply_str;
                            reply_str = reply_buf.SerializeAsString();
                            respond(reply_str);
                        });
                    }
                })->start();
            }
            do_accept();
        });
}

KVStore::KVStore() {}

std::string KVStore::to_buf(KVRequest req) {
    proto::kv::KVRequest req_buf;
    std::string req_str;
    req_buf.set_key(req.key);
    req_buf.set_value(req.value);
     switch(req.type) {
        case KVRequestType::GET:
            req_buf.set_type(proto::kv::KVRequest_RequestType_GET);
            break;
        case KVRequestType::PUT:
            req_buf.set_type(proto::kv::KVRequest_RequestType_PUT);
            break;
        case KVRequestType::DEL:
            req_buf.set_type(proto::kv::KVRequest_RequestType_DEL);
            break;
    }
    req_str = req_buf.SerializeAsString();
    return req_str;
}

KVRequest KVStore::from_buf(std::string data) {
    proto::kv::KVRequest req_buf;
    req_buf.ParseFromString(data);
    KVRequest req;
    switch(req_buf.type()) {
        case proto::kv::KVRequest_RequestType_GET:
            req.type = KVRequestType::GET;
            break;
        case proto::kv::KVRequest_RequestType_PUT:
            req.type = KVRequestType::PUT;
            break;
        case proto::kv::KVRequest_RequestType_DEL:
            req.type = KVRequestType::DEL;
            break;
    }
    req.key = req_buf.key();
    req.value = req_buf.value();
    return req;
}

KVReply KVStore::handle_request(KVRequest req) {
    std::string val;
    bool success = true;
    switch(req.type) {
        case KVRequestType::GET:
            if(store_.find(req.key) != store_.end()) {
                val = store_[req.key];
                success = true;
            } else {
                success = false;
            }
            break;
        case KVRequestType::PUT:
            store_[req.key] = req.value;
            break;
        case KVRequestType::DEL:
            store_.erase(req.key);
            break;
    };
    return KVReply{
        val,
        0,
        success,
    };
}