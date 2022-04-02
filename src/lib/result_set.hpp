#pragma once

#include <algorithm>
#include <cmath>
#include <list>
#include <string>
#include <vector>

#include "client.hpp"
#include "site.hpp"

using namespace std;

static constexpr double MIGRATE_FACTOR = 0.8;
inline int FactorSep(int sep) { return static_cast<int>(MIGRATE_FACTOR * sep); }

// 一天中所有客户的分配情况
class Result {
    friend class ResultSet;

  public:
    Result() = default;
    explicit Result(const vector<Client> &clients, const vector<Site> &sites) {
        cli_tbls_.reserve(clients.size());
        for (const auto &cli : clients) {
            cli_tbls_.push_back(cli.GetAllocationTable());
        }
        site_loads_.reserve(sites.size());
        site_streams_.reserve(sites.size());
        for (const auto &site : sites) {
            site_loads_.push_back(site.GetAllocatedBandwidth());
            site_streams_.push_back(site.GetStreams());
        }
    }
    size_t GetClientAccessibleSiteCount(size_t C) const {
        return cli_tbls_[C].tbl.size();
    }
    const list<Stream> &GetAllocationTable(size_t C, size_t S) const {
        return cli_tbls_[C].tbl[S];
    }
    // migrate streams from server[From] to other accessible servers
    bool Migrate(size_t From, vector<pair<int, size_t>> &seps,
                 std::vector<std::vector<size_t>> &cli_refs) {
        bool ret = false;
        int cur_sep = seps[From].first;
        for (auto it = site_streams_[From].begin(); it != site_streams_[From].end();) {
            // which client is the stream from
            auto &cli_tbl = cli_tbls_[it->cli_idx];
            auto &cli_ref = cli_refs[it->cli_idx];
            size_t To = -1;
            // choose To server to move
            for (size_t candidate : cli_ref) {
                if (candidate == From) {
                    continue;
                }
                // if server[To] used size is less than factor * cap
                if (site_loads_[candidate] + it->stream_size < FactorSep(seps[candidate].first)) {
                    To = candidate;
                    break;
                }
            }
            // if all other site's load is greater
            if (To == -1) {
                it++;
                continue;
            }
            cur_sep -= it->stream_size;
            cli_tbl.MoveStream(*it, From, To);
            site_streams_[To].push_back(*it);
            it = site_streams_[From].erase(it);
            if (cur_sep < FactorSep(seps[From].first)) {
                ret = true;
                break;
            }
        }
        return ret;
    }

  private:
    vector<AllocationTable> cli_tbls_;
    vector<int> site_loads_;
    vector<list<Stream>> site_streams_;
};

// 所有天的客户分配情况
class ResultSet {
    using ResultSetIter = std::vector<Result>::iterator;

  public:
    ResultSet() = default;
    ResultSet(const std::vector<Site> &sites, std::vector<Client> &clis,
              int base)
        : base_(base) {
        sites_caps_.reserve(sites.size());
        for (const auto &site : sites) {
            sites_caps_.push_back(site.GetTotalBandwidth());
        }
        cli_ref_sites_idx_.reserve(clis.size());
        for (const auto &cli : clis) {
            cli_ref_sites_idx_.push_back(cli.GetAccessibleSite());
        }
    }
    void Migrate();
    void Reserve(size_t n) { days_result_.reserve(n); }
    void AddResult(Result &&day_res) { days_result_.push_back(day_res); }
    int GetGrade();
    ResultSetIter begin() { return days_result_.begin(); }
    ResultSetIter end() { return days_result_.end(); }

  private:
    std::vector<Result> days_result_;
    std::vector<bool> is_always_empty_;
    // 没一台服务器，当前的<95分位值，对应的天>
    std::vector<pair<int, size_t>> seps_;
    std::vector<int> sites_caps_;
    // 每一个服务器，需要迁移的所有<流量大小，对应的天>
    // 从95分位值 到 FACTOR * 95分位值
    std::vector<std::list<pair<int, size_t>>> site_migrate_days_;
    std::vector<std::vector<size_t>> cli_ref_sites_idx_;
    int base_{0};

    void ComputeAllSeps(bool set_migrate = false);
};

int ResultSet::GetGrade() {
    ComputeAllSeps(false);
    int grade = 0;
    printf("all 95 seperators:\n");
    for (auto &sep : seps_) {
        printf("%d ", sep.first);
    }
    printf("\n");
    for (size_t S = 0; S < seps_.size(); S++) {
        if (is_always_empty_[S]) {
            continue;
        }
        if (seps_[S].first <= base_) {
            grade += base_;
        } else {
            grade += static_cast<int>(pow(1.0 * (seps_[S].first - base_), 2) /
                                          sites_caps_[S] +
                                      seps_[S].first);
        }
    }
    return grade;
}

void ResultSet::Migrate() {
    ComputeAllSeps(true);
    for (size_t S = 0; S < site_migrate_days_.size(); S++) {
        auto &mig_streams = site_migrate_days_[S];
        for (auto it = mig_streams.begin(); it != mig_streams.end();) {
            size_t day = it->second;
            // 将第day天的S号服务器的请求分配到其他服务器
            bool ret = days_result_[day].Migrate(S, seps_, cli_ref_sites_idx_);
            if (ret == false) {
                break;
            }
            it = mig_streams.erase(it);
            // 更新sep值，避免其他服务器又打到已经migrate过的服务器
            seps_[S] = mig_streams.front();
        }
    }
}

void ResultSet::ComputeAllSeps(bool set_migrate) {
    assert(not days_result_.empty());
    size_t site_count = days_result_[0].site_loads_.size();
    seps_.resize(site_count, {0, 0});
    if (set_migrate) {
        site_migrate_days_.resize(site_count, list<pair<int, size_t>>{});
    }
    is_always_empty_.resize(site_count, false);
    for (size_t S = 0; S < site_count; S++) {
        // load, day
        vector<pair<int, size_t>> arr;
        arr.clear();
        arr.reserve(days_result_.size());
        for (size_t day = 0; day < days_result_.size(); day++) {
            arr.push_back({days_result_[day].site_loads_[S], day});
        }
        std::sort(arr.begin(), arr.end(),
                  [](const pair<int, size_t> &l, const pair<int, size_t> &r) {
                      return l.first < r.first;
                  });
        size_t sep_idx = static_cast<size_t>(arr.size() * 0.95);
        seps_[S] = arr[sep_idx];
        if (arr.back().first == 0) {
            is_always_empty_[S] = true;
        }
        // get migrate days
        if (set_migrate) {
            for (int i = static_cast<int>(sep_idx); i >= 0; i--) {
                if (arr[i].first <=
                    static_cast<int>(arr[sep_idx].first * MIGRATE_FACTOR)) {
                    break;
                }
                site_migrate_days_[S].push_back(arr[i]);
            }
        }
    }
}
