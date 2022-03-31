#pragma once

#include <cstdio>
#include <list>
#include <numeric>
#include <string>
#include <vector>

using namespace std;

class Client {
    friend class FileParser;

  public:
    Client() = default;
    Client(const string &name) : name_(name) {}

    // 初始化其他内部模块
    void Init() {
        size_t size = accessible_sites_.size();
        allocation_table_.resize(size, list<string>{});
    }
    void Reset() {
        for (auto &elem : allocation_table_) {
            elem = list<string>{};
        }
    }

    const char *GetName() const { return name_.c_str(); }

    size_t GetSiteCount() const { return accessible_sites_.size(); }
    int GetSiteIndex(int idx) const { return accessible_sites_[idx]; }
    vector<size_t> &GetAccessibleSite() { return accessible_sites_; }

    const vector<list<string>> &GetAllocationTable() const {
        return allocation_table_;
    }

    int GetAccessTotal() { return accessible_total; }
    void AddAccessTotal(int value) { accessible_total += value; }

    void AddAllocation(size_t idx, string stream_name) {
        allocation_table_[idx].push_back(stream_name);
    }
    void AddAllocationBySiteIndex(size_t site_idx, string stream_name) {
        for (size_t i = 0; i < allocation_table_.size(); i++) {
            if (accessible_sites_[i] == site_idx) {
                AddAllocation(i, stream_name);
                break;
            }
        }
    }

  private:
    string name_;
    vector<size_t> accessible_sites_; // 可以访问到的服务器集合的index
    vector<list<string>>
        allocation_table_; // 在一次请求中分配到每个服务器的流量值
    int accessible_total{0};
};
