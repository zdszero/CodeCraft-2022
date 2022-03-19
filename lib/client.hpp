#pragma once

#include <string>
#include <vector>
#include <cstdio>
#include <numeric>

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
    const std::vector<int>& GetAccessibleSite() const {
        return accessible_sites_;
    }
    size_t GetSiteCount() const { return accessible_sites_.size(); }
    int GetSiteAllocation(int idx) const { return allocation_table_[idx]; }
    int GetTotalAllocation() const { return std::accumulate(allocation_table_.begin(), allocation_table_.end(), 0); }
    void AddAllocation(int idx, int value) {
        allocation_table_[idx] += value;
    }
    bool AddAllocationBySite(int site, int value) {
        bool flag = false;
        for (size_t i = 0; i < accessible_sites_.size(); i++) {
            if (accessible_sites_[i] == site) {
                allocation_table_[i] += value;
                flag = true;
                break;
            }
        }
        return flag;
    }
    int GetSiteIndex(int idx) const { return accessible_sites_[idx]; }
    double GetRatio(int idx) const { return ratios_[idx]; }
    void SetRatio(int idx, double ratio) { ratios_[idx] = ratio; }
    void Reset() {
        for (auto &elem : allocation_table_) {
            elem = 0;
        }
    }

  private:
    std::string name_;
    std::vector<int> accessible_sites_; // 可以访问到的服务器集合
    std::vector<int> allocation_table_; // 在一次请求中分配到每个服务器的流量值
    std::vector<double> ratios_; // 分配流量的衡量变量
};
