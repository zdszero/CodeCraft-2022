#pragma once

#include <string>
#include <vector>

class Site {
    friend class FileParser;

  public:
    Site() = default;
    Site(const std::string &name, int bandwidth)
        : name_(name), total_bandwidth_(bandwidth),
          remain_bandwidth(bandwidth) {}
    Site(const std::string &name, int bandwidth, std::vector<int> &&ref_clients)
        : name_(name), ref_clients_(std::move(ref_clients)),
          total_bandwidth_(bandwidth), remain_bandwidth(bandwidth) {}
    const char *GetName() const { return name_.c_str(); }
    int GetRefTimes() const { return ref_times_; }
    void AddRefClient(int client_id) {
        ref_clients_.push_back(client_id);
        ref_times_++;
    }
    int GetTotalBandwidth() const { return total_bandwidth_; }
    int GetRemainBandwidth() const { return remain_bandwidth; }
    void DecreaseBandwith(int usage) { remain_bandwidth -= usage; }
    void ResetRemainBandwidth() { remain_bandwidth = total_bandwidth_; }
    const std::vector<int> &GetRefClients() const { return ref_clients_; }
    void SetMaxFullTimes(int times) { max_full_times_ = times; }
    int GetFullTimes() { return cur_full_times_; }
    void IncFullTimes() { cur_full_times_--; }
    bool IsSafe() const { return cur_full_times_ < max_full_times_; }

  private:
    std::string name_;
    int ref_times_{0}; // 可以被多少个client访问
    std::vector<int> ref_clients_;
    int total_bandwidth_{0};
    int remain_bandwidth{0};
    int max_full_times_{0};
    int cur_full_times_{0};
};
