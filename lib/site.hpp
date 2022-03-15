#pragma once

#include <string>

class Site {
    friend class FileParser;

  public:
    Site() = default;
    Site(const std::string &name, int bandwidth)
        : name_(name), total_bandwidth_(bandwidth),
          remain_bandwidth(bandwidth) {}
    const char *GetName() const { return name_.c_str(); }
    int GetRefTimes() const { return ref_times_; }
    void IncRefTimes() { ref_times_++; }
    int GetTotalBandwidth() const { return total_bandwidth_; }
    int GetRemainBandwidth() const { return remain_bandwidth; }
    void DecreaseBandwith(int usage) { remain_bandwidth -= usage; }

  private:
    std::string name_;
    int ref_times_{0}; // 可以被多少个client访问
    int total_bandwidth_{0};
    int remain_bandwidth{0};
};
