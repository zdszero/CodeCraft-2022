#pragma once

#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

class Demand {
  friend class FileParser;

public:
  Demand() = default;
  string GetTime() { return time_; }
  unordered_map<string, vector<int>> &GetStreamDemands() { return demands_; }
  long GetTotalDemand() const {
    long ans = 0;
    for (auto it = demands_.begin(); it != demands_.end(); it++) {
      ans += accumulate(it->second.begin(), it->second.end(), 0L);
    }
    return ans;
  }
  long GetClientDemand(size_t C) const {
    long ans = 0;
    for (auto it = demands_.begin(); it != demands_.end(); it++) {
      ans += it->second[C];
    }
    return ans;
  }

private:
  string time_;
  unordered_map<string, vector<int>> demands_;
};
