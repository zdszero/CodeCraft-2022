#pragma once

#include <cassert>
#include <list>
#include <numeric>
#include <string>
#include <vector>

#include "stream.hpp"

using namespace std;

// 服务器到其每个可以访问到的结点的分配情况
struct AllocationTable {
    vector<list<Stream>> tbl;
    const list<Stream> &GetList(size_t S) { return tbl[S]; }
    void Add(size_t S, const  Stream &p) {
        tbl[S].push_back(p);
    }
    void MoveStream(const Stream &stream, size_t from, size_t to) {
        bool flag = false;
        Stream tmp{};
        for (auto it = tbl[from].begin(); it != tbl[from].end(); it++) {
            if (*it == stream) {
                tmp = *it;
                tbl[from].erase(it);
                flag = true;
                break;
            }
        }
        tbl[to].push_back(tmp);
        assert(flag);
    }
};

class Client {
    friend class FileParser;

  public:
    Client() = default;
    Client(const string &name) : name_(name) {}

    // 初始化其他内部模块
    void Init() {
        size_t size = accessible_sites_.size();
        alloc_.tbl.resize(size, list<Stream>{});
    }
    void Reset() {
        for (auto &l : alloc_.tbl) {
            l.clear();
        }
    }

    const char *GetName() const { return name_.c_str(); }

    size_t GetSiteCount() const { return accessible_sites_.size(); }
    int GetSiteIndex(int idx) const { return accessible_sites_[idx]; }
    vector<size_t> &GetAccessibleSite() { return accessible_sites_; }
    const vector<size_t> &GetAccessibleSite() const { return accessible_sites_; }

    const AllocationTable &GetAllocationTable() const {
        return alloc_;
    }

    int GetAccessTotal() { return accessible_total; }
    void AddAccessTotal(int value) { accessible_total += value; }

    void AddAllocation(size_t idx, const Stream &stream) {
        alloc_.tbl[idx].push_back(stream);
    }
    void AddAllocationBySiteIndex(size_t site_idx, const Stream &stream) {
        bool flag = false;
        for (size_t i = 0; i < alloc_.tbl.size(); i++) {
            if (accessible_sites_[i] == site_idx) {
                AddAllocation(i, stream);
                flag = true;
                break;
            }
        }
        assert(flag == true);
    }

  private:
    string name_;
    vector<size_t> accessible_sites_; // 可以访问到的服务器集合的index
    AllocationTable alloc_;
    int accessible_total{0};
};
