#pragma once

#include <algorithm>
#include <cassert>
#include <list>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

#include "stream.hpp"

using namespace std;

// 服务器到其每个可以访问到的结点的分配情况
struct AllocationTable {
    vector<list<Stream>> tbl;
    // from site no to site index
    unordered_map<size_t, size_t> site_map;

    void Add(size_t n, const Stream &p) { tbl[n].push_back(p); }
    list<Stream> &GetList(size_t site_idx) {
        size_t S = site_map[site_idx];
        return tbl[S];
    }
    void MoveStream(const Stream &stream, size_t from, size_t to) {
        bool flag = false;
        size_t from_idx = site_map[from];
        size_t to_idx = site_map[to];
        Stream stream_cpy{stream};
        for (auto it = tbl[from_idx].begin(); it != tbl[from_idx].end(); it++) {
            if (*it == stream_cpy) {
                tbl[from_idx].erase(it);
                flag = true;
                break;
            }
        }
        stream_cpy.site_idx = to;
        tbl[to_idx].push_back(stream_cpy);
        assert(flag);
    }
};

class Client {
    friend class FileParser;

  public:
    Client() = default;
    Client(size_t id, const string &name) : id_(id), name_(name) {}

    // 初始化其他内部模块
    void Init() {
        size_t size = accessible_sites_.size();
        alloc_.tbl.resize(size, list<Stream>{});
        for (size_t i = 0; i < accessible_sites_.size(); i++) {
            alloc_.site_map[accessible_sites_[i]] = i;
        }
    }
    void ReInit() {
        for (size_t i = 0; i < accessible_sites_.size(); i++) {
            alloc_.site_map[accessible_sites_[i]] = i;
        }
    }
    void Reset() {
        for (auto &l : alloc_.tbl) {
            l.clear();
        }
    }
    size_t GetID() const { return id_; }
    void SetID(size_t id) { id_ = id; }

    const char *GetName() const { return name_.c_str(); }

    size_t GetSiteCount() const { return accessible_sites_.size(); }
    int GetSiteIndex(int idx) const { return accessible_sites_[idx]; }
    vector<size_t> &GetAccessibleSite() { return accessible_sites_; }
    const vector<size_t> &GetAccessibleSite() const {
        return accessible_sites_;
    }

    const AllocationTable &GetAllocationTable() const { return alloc_; }

    int GetAccessTotal() { return accessible_total; }
    void AddAccessTotal(int value) { accessible_total += value; }

    void AddStream(size_t idx, const Stream &stream) {
        assert(stream.site_idx == accessible_sites_[idx]);
        assert(id_ == stream.cli_idx);
        alloc_.tbl[idx].push_back(stream);
    }
    void AddStreamBySiteIndex(size_t site_idx, const Stream &stream) {
        bool flag = false;
        for (size_t i = 0; i < alloc_.tbl.size(); i++) {
            if (accessible_sites_[i] == site_idx) {
                AddStream(i, stream);
                flag = true;
                break;
            }
        }
        assert(flag == true);
        assert(stream.site_idx == site_idx);
        assert(id_ == stream.cli_idx);
    }
    void PrintSites() {
        auto sites = accessible_sites_;
        sort(sites.begin(), sites.end());
        printf("client %ld: ", id_);
        for (size_t s : sites) {
            printf("%ld ", s);
        }
        printf("\n");
    }

  private:
    size_t id_;
    string name_;
    vector<size_t> accessible_sites_; // 可以访问到的服务器集合的index
    AllocationTable alloc_;
    int accessible_total{0};
};
