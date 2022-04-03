#pragma once

#include <cassert>
#include <list>
#include <string>
#include <vector>

#include "stream.hpp"

using namespace std;

class Site {
    friend class FileParser;

  public:
    Site() = default;
    Site(size_t id, const string &name, int bandwidth)
        : id_(id), name_(name), total_bandwidth_(bandwidth),
          remain_bandwidth(bandwidth) {}
    const char *GetName() const { return name_.c_str(); }
    int GetRefTimes() const { return ref_times_; }
    int GetFullTimes() const { return cur_full_times_; }
    int GetTotalBandwidth() const { return total_bandwidth_; }
    int GetRemainBandwidth() const { return remain_bandwidth; }
    int GetAllocatedBandwidth() const {
        return total_bandwidth_ - remain_bandwidth;
    }
    int GetSeperateBandwidth() const { return static_cast<int>(seperate_); }
    void SetSeperateBandwidth(int sep) { seperate_ = sep; }
    const vector<size_t> &GetRefClients() const { return ref_clients_; }
    vector<size_t> &GetRefClients() { return ref_clients_; }

    void AddRefClient(size_t client_id) {
        ref_clients_.push_back(client_id);
        ref_times_++;
    }
    void DecreaseBandwidth(int usage) {
        remain_bandwidth -= usage;
        assert(remain_bandwidth >= 0);
    }
    void Reset() {
        remain_bandwidth = total_bandwidth_;
        full_this_time_ = false;
        streams_.clear();
    }
    void Restart() {
        remain_bandwidth = total_bandwidth_;
        full_this_time_ = false;
        // seperate_ = 0;
        cur_full_times_ = 0;
    }
    void SetMaxFullTimes(int times) { max_full_times_ = times; }
    void IncFullTimes() { cur_full_times_++; }
    bool IsSafe() const { return cur_full_times_ < max_full_times_; }
    bool IsFullThisTime() const { return full_this_time_; }
    void SetFullThisTime() { full_this_time_ = true; }
    void SetCurFullTimes(int full) { cur_full_times_ = 0; }
    void ResetSeperateBandwidth() {
        if (full_this_time_) {
            return;
        }
        if (GetAllocatedBandwidth() > seperate_) {
            seperate_ = GetAllocatedBandwidth();
        }
    }
    void AddStream(const Stream &stream) {
        assert(stream.site_idx == id_);
        bool flag = false;
        for (size_t cli_idx : ref_clients_) {
            if (cli_idx == stream.cli_idx) {
                flag = true;
                break;
            }
        }
        assert(flag == true);
        DecreaseBandwidth(stream.stream_size);
        streams_.push_back(stream);
    }
    const list<Stream> &GetStreams() const { return streams_; }

  private:
    static constexpr double FACTOR = 0.8;
    size_t id_;
    string name_;
    int ref_times_{0}; // 可以被多少个client访问
    vector<size_t> ref_clients_;
    int total_bandwidth_{0};
    int remain_bandwidth{0};
    int max_full_times_{0};
    int cur_full_times_{0};
    int seperate_{0};
    bool full_this_time_{false};
    // client idx | stream name | stream size
    list<Stream> streams_;
};
