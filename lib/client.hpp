#pragma once

#include <string>
#include <vector>
#include <cstdio>

class Client {
    friend class FileParser;

  public:
    Client() = default;
    Client(const std::string &name) : name_(name) {}

    // 初始化其他内部模块
    void Init() {
        size_t size = accessible_sites_.size();
        allocation_table_.resize(size, 0);
        ratios_.resize(size, 0);
    }
    const char *GetName() const { return name_.c_str(); }
    const std::vector<int> GetAccessibleSite() const {
        return accessible_sites_;
    }
    size_t GetSiteCount() const { return accessible_sites_.size(); }
    int GetSiteAllocation(int idx) const { return allocation_table_[idx]; }
    void SetSiteAllocation(int idx, int value) {
        allocation_table_[idx] = value;
    }
    int GetSiteIndex(int idx) const { return accessible_sites_[idx]; }
    double GetRatio(int idx) const { return ratios_[idx]; }
    void SetRatio(int idx, double ratio) { ratios_[idx] = ratio; }

  private:
    std::string name_;
    std::vector<int> accessible_sites_;
    std::vector<int> allocation_table_;
    std::vector<double> ratios_;
};
