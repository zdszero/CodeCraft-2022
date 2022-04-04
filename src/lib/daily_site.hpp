#pragma once

#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

class daily_site {
  friend class FileParser;

public:
  daily_site() = default;
  daily_site(size_t time, size_t site_idx, int total, int bandwidth)
      : time_(time), site_idx_(site_idx), total_(total),
        total_bandwidth_(bandwidth), remain_bandwidth(bandwidth) {}

  void SetTotal(int total) { total_ = total; }
  int GetTime() const { return time_; }
  int GetSiteIdx() const { return site_idx_; }
  int GetTotal() const { return total_; }
  int GetTotalBandwidth() const { return total_bandwidth_; }
  int GetRemainBandwidth() const { return remain_bandwidth; }
  void DecreaseBandwith(int usage) { remain_bandwidth -= usage; }

private:
  size_t time_{0}; // time slot
  int site_idx_{0};
  int total_{0}; //分配到的总流量

  int total_bandwidth_{0};
  int remain_bandwidth{0};
};
