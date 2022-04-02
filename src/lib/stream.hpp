#pragma once

#include <string>
using namespace std;

struct Stream {
    size_t cli_idx;
    string stream_name;
    int stream_size;
    Stream() = default;
    Stream(size_t idx, const string &name, int size)
        : cli_idx(idx), stream_name(name), stream_size(size) {}
    bool operator==(const Stream &rhs) {
        return ((cli_idx == rhs.cli_idx) && (stream_name == rhs.stream_name) &&
                (stream_size == rhs.stream_size));
    }
};
