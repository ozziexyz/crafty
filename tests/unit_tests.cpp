// Minimal, dependency-free test runner for KVStore + the proto round-trip.
//
// Deliberately not using assert(): CMAKE_BUILD_TYPE=Release (our default)
// defines NDEBUG, which compiles asserts out entirely — a silent no-op suite
// is worse than no suite. CHECK() below always runs, prints PASS/FAIL per
// check, and the binary's exit code reflects the result (0 = all passed),
// so this works under `ctest` regardless of build type.

#include <iostream>
#include <string>
#include "kv.h"

static int g_failures = 0;

#define CHECK(cond) do { \
    if (cond) { \
        std::cout << "  PASS: " << #cond << std::endl; \
    } else { \
        std::cout << "  FAIL: " << #cond << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        g_failures++; \
    } \
} while (0)

static void test_put_get() {
    std::cout << "test_put_get" << std::endl;
    KVStore store;
    KVReply put_reply = store.handle_request(KVRequest{KVRequestType::PUT, "k1", "v1"});
    CHECK(put_reply.success);

    KVReply get_reply = store.handle_request(KVRequest{KVRequestType::GET, "k1", ""});
    CHECK(get_reply.success);
    CHECK(get_reply.value == "v1");
}

static void test_get_missing_key() {
    std::cout << "test_get_missing_key" << std::endl;
    KVStore store;
    KVReply reply = store.handle_request(KVRequest{KVRequestType::GET, "nope", ""});
    CHECK(!reply.success);
}

static void test_put_overwrites() {
    std::cout << "test_put_overwrites" << std::endl;
    KVStore store;
    store.handle_request(KVRequest{KVRequestType::PUT, "k1", "v1"});
    store.handle_request(KVRequest{KVRequestType::PUT, "k1", "v2"});
    KVReply reply = store.handle_request(KVRequest{KVRequestType::GET, "k1", ""});
    CHECK(reply.success);
    CHECK(reply.value == "v2");
}

static void test_del() {
    std::cout << "test_del" << std::endl;
    KVStore store;
    store.handle_request(KVRequest{KVRequestType::PUT, "k1", "v1"});
    KVReply del_reply = store.handle_request(KVRequest{KVRequestType::DEL, "k1", ""});
    CHECK(del_reply.success);

    KVReply get_reply = store.handle_request(KVRequest{KVRequestType::GET, "k1", ""});
    CHECK(!get_reply.success);
}

static void test_del_missing_key() {
    std::cout << "test_del_missing_key" << std::endl;
    KVStore store;
    // DEL on a key that was never set shouldn't crash or report failure —
    // std::unordered_map::erase() on a missing key is a harmless no-op.
    KVReply reply = store.handle_request(KVRequest{KVRequestType::DEL, "nope", ""});
    CHECK(reply.success);
}

static void test_proto_roundtrip_embedded_null() {
    std::cout << "test_proto_roundtrip_embedded_null" << std::endl;
    KVStore store;
    // The bug this guards against: the client used to parse replies with
    // ParseFromString(buf.data()), a const char* -> std::string conversion
    // that stops at the first '\0'. Protobuf-encoded output routinely
    // contains embedded nulls, so this is the regression test for it.
    std::string value("a\0b\0c", 5);
    KVRequest req{KVRequestType::PUT, "k1", value};

    std::string buf = store.to_buf(req);
    KVRequest round_tripped = store.from_buf(buf);

    CHECK(round_tripped.type == KVRequestType::PUT);
    CHECK(round_tripped.key == "k1");
    CHECK(round_tripped.value.size() == 5);
    CHECK(round_tripped.value == value);
}

int main() {
    test_put_get();
    test_get_missing_key();
    test_put_overwrites();
    test_del();
    test_del_missing_key();
    test_proto_roundtrip_embedded_null();

    std::cout << std::endl;
    if (g_failures == 0) {
        std::cout << "All checks passed." << std::endl;
        return 0;
    } else {
        std::cout << g_failures << " check(s) failed." << std::endl;
        return 1;
    }
}
