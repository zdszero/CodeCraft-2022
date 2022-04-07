#pragma once

#include <set>
#include <algorithm>
#include <cmath>
#include <list>
#include <string>
#include <vector>

#include "client.hpp"
#include "site.hpp"

using namespace std;

class ResultSet;

// 一天中所有客户的分配情况
class Result {
    friend class ResultSet;

  public:
    Result() = default;
    explicit Result(size_t day, const vector<Client> &clients, const vector<Site> &sites): day_(day) {
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
    int Migrate(size_t from, vector<pair<int, size_t>> &seps,
                std::vector<std::vector<size_t>> &cli_refs, int base,
                int base_cost, vector<int> &sites_cap, int day, bool isSep) {
        int cur_load = site_loads_[from];

        for (auto it = site_streams_[from].begin();
             it != site_streams_[from].end();) {
            assert(it->site_idx == from);
            // which client is the stream from
            auto &cli_ref = cli_refs[it->cli_idx];
            int To = -1;
            int min_free = numeric_limits<int>::max();
            /* int max_dec_cost = 0; */
            // choose To server to move
            for (size_t candidate : cli_ref) {
                if (candidate == from) {
                    continue;
                }
                // if server[To] used size is less than factor * cap
                int free = seps[candidate].first - site_loads_[candidate] -
                           it->stream_size;
                if (free < min_free && free >= 0) {
                    min_free = free;
                    To = static_cast<int>(candidate);
                    break;
                }
            }
            // if all other site's load is greater
            if (To == -1) {
                it++;
                continue;
            }
            cur_load -= it->stream_size;
            it = MoveStream(it, from, To);
            if (cur_load <= base) {
                // ret = true;
                break;
            }
        }
        return cur_load;
    }

    void ExpelTop5(size_t from, vector<pair<int, size_t>> &seps, vector<vector<size_t>> &cli_refs) {
        for (auto it = site_streams_[from].begin(); it != site_streams_[from].end();) {
            assert(it->site_idx == from);
            // which client is the stream from
            auto &cli_ref = cli_refs[it->cli_idx];
            int to = -1;
            // choose To server to move
            for (size_t candidate : cli_ref) {
                if (candidate == from) {
                    continue;
                }
                if (site_loads_[candidate] + it->stream_size < seps[candidate].first) {
                    to = candidate;
                    break;
                }
            }
            // if all other site's load is greater
            if (to == -1) {
                it++;
                continue;
            }
            it = MoveStream(it, from, to);
        }
    }

    void UpdateTop5(size_t to, vector<pair<int, size_t>> &seps, vector<vector<size_t>> &cli_refs) {

    }

    list<Stream>::iterator MoveStream(list<Stream>::iterator &it, size_t from, size_t to) {
            auto &cli_tbl = cli_tbls_[it->cli_idx];
            cli_tbl.MoveStream(*it, from, to);
            site_loads_[to] += it->stream_size;
            site_loads_[from] -= it->stream_size;
            it->site_idx = to;
            site_streams_[to].push_back(*it);
            return site_streams_[from].erase(it);
    }

  private:
    size_t day_;
    vector<AllocationTable> cli_tbls_;
    vector<int> site_loads_;
    vector<list<Stream>> site_streams_;
};

// 所有天的客户分配情况
class ResultSet {
    friend class Result;
    using ResultSetIter = std::vector<Result>::iterator;
    enum class ComputeJob {
        GET_GRADE,
        GET_95,
        GET_5
    };

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
    void ExpelTop5();
    void UpdateTop5();
    void Reserve(size_t n) { days_result_.reserve(n); }
    void Resize(size_t n) { days_result_.resize(n); }
    void AddResult(Result &&day_res) { days_result_.push_back(day_res); }
    void SetResult(size_t day, Result &&day_res) {
        days_result_[day] = std::move(day_res);
    }
    int GetGrade();
    ResultSetIter begin() { return days_result_.begin(); }
    ResultSetIter end() { return days_result_.end(); }
    
    void PrintLoads();

  private:
    std::vector<Result> days_result_;
    std::vector<bool> is_always_empty_;
    // 没一台服务器，当前的<95分位值，对应的天>
    std::vector<pair<int, size_t>> seps_;
    std::vector<int> sites_caps_;
    // 每一个服务器，需要迁移的所有<流量大小，对应的天>
    // 从95分位值 到 FACTOR * 95分位值
    std::vector<std::list<pair<int, size_t>>> site_migrate_days_;
    std::vector<std::list<pair<int, size_t>>> site_top5_days_;
    std::vector<std::vector<size_t>> cli_ref_sites_idx_;
    int base_{0};

    void ComputeAllSeps(ComputeJob job);
};

inline int ResultSet::GetGrade() {
    ComputeAllSeps(ComputeJob::GET_GRADE);
    int grade = 0;
    printf("all 95 seperators:\n");
    int cnt = 0;
    printf("0: ");
    for (auto &sep : seps_) {
        printf("%5d ", sep.first);
        if (++cnt % 5 == 0) {
            printf("\n%d: ", cnt);
        }
    }
    printf("\n");
    int total = 0;
    for (size_t S = 0; S < seps_.size(); S++) {
        if (is_always_empty_[S]) {
            continue;
        }
        total += seps_[S].first;
        if (seps_[S].first <= base_) {
            grade += base_;
        } else {
            grade += static_cast<int>(pow(1.0 * (seps_[S].first - base_), 2) /
                                          sites_caps_[S] +
                                      seps_[S].first);
        }
    }
    printf("total:%d\n", total);
    return grade;
}

inline void ResultSet::Migrate() {
    ComputeAllSeps(ComputeJob::GET_95);
    vector<size_t> site_indexes(site_migrate_days_.size(), 0);
    for (size_t i = 0; i < site_indexes.size(); i++) {
        site_indexes[i] = i;
    }
    sort(site_indexes.begin(), site_indexes.end(), [this](size_t l, size_t r) {
        auto getval = [this](size_t i) -> double {
            long sum = 0;
            for (auto p : site_migrate_days_[i]) {
                sum += p.first;
            }
            long ave = (sum * 1.0) / (site_migrate_days_[i].size() * 1.0);
            long vari = 0;

            for (const auto &p : site_migrate_days_[i]) {
                vari += pow((p.first - ave), 2);
            }
            return (vari * 1.0) / (site_migrate_days_[i].size() * 1.0);
        };
        return getval(l) > getval(r);
    });
    for (auto site_idx : site_indexes) {
        auto &site_mig_day = site_migrate_days_[site_idx];
        int base = base_; //
        int cur_used = 0;
        int sep_day = -1;
        bool isSep = true;
        for (auto it = site_mig_day.begin(); it != site_mig_day.end();) {
            size_t day = it->second;
            if (it->first <= base) {
                break;
            }
            cur_used =
                days_result_[day].Migrate(site_idx, seps_, cli_ref_sites_idx_,
                                          base, base_, sites_caps_, day, isSep);
            isSep = false;
            if (cur_used >= base) {
                base = cur_used;
                sep_day = day;
            }
            it = site_mig_day.erase(it);
            if (site_mig_day.empty()) {
                break;
            }
        }
        if (sep_day != -1) {
            seps_[site_idx] = {base, sep_day};
        }
    }
}

inline void ResultSet::ExpelTop5() {
    ComputeAllSeps(ComputeJob::GET_5);
    for (size_t site_idx = 0; site_idx < site_top5_days_.size(); site_idx++) {
        for (auto &p : site_top5_days_[site_idx]) {
            size_t day = p.second;
            days_result_[day].ExpelTop5(site_idx, seps_, cli_ref_sites_idx_);
        }
    }
}

inline void ResultSet::UpdateTop5() {
    ComputeAllSeps(ComputeJob::GET_5);
    for (size_t site_idx = 0; site_idx < site_top5_days_.size(); site_idx++) {
        for (auto &p : site_top5_days_[site_idx]) {
            size_t day = p.second;
            days_result_[day].UpdateTop5(site_idx, seps_, cli_ref_sites_idx_);
        }
    }
}

inline void ResultSet::ComputeAllSeps(ComputeJob job) {
    assert(not days_result_.empty());
    size_t site_count = days_result_[0].site_loads_.size();
    seps_.resize(site_count, {0, 0});
    if (job == ComputeJob::GET_95) {
        site_migrate_days_.resize(site_count, list<pair<int, size_t>>{});
        for (auto &l : site_migrate_days_) {
            l.clear();
        }
    } else if (job == ComputeJob::GET_5) {
        site_top5_days_.resize(site_count, list<pair<int, size_t>>{});
        for (auto &l : site_top5_days_) {
            l.clear();
        }
    }
    is_always_empty_.resize(site_count, false);
    for (size_t site_idx = 0; site_idx < site_count; site_idx++) {
        // load, day
        vector<pair<int, size_t>> arr;
        arr.clear();
        arr.reserve(days_result_.size());
        for (size_t day = 0; day < days_result_.size(); day++) {
            arr.push_back({days_result_[day].site_loads_[site_idx], day});
        }
        std::sort(arr.begin(), arr.end(),
                  [](const pair<int, size_t> &l, const pair<int, size_t> &r) {
                      return l.first < r.first;
                  });
        size_t sep_idx = ceil(arr.size() * 0.95) - 1;
        seps_[site_idx] = arr[sep_idx];
        if (arr.back().first == 0) {
            is_always_empty_[site_idx] = true;
        }
        // get migrate days
        if (job == ComputeJob::GET_95) {
            if (static_cast<int>(arr[sep_idx].first) <= base_) {
                continue;
            }
            for (int i = static_cast<int>(sep_idx); i >= 0; i--) {
                if (arr[i].first <= base_) {
                    break;
                }
                site_migrate_days_[site_idx].push_back(arr[i]);
            }
        } else if (job == ComputeJob::GET_5) {
            for (size_t i = sep_idx + 1; i < arr.size(); i++) {
                site_top5_days_[site_idx].push_back(arr[i]);
            }
        }
    }
}

inline void ResultSet::PrintLoads() {
    assert(not days_result_.empty());
    size_t site_count = days_result_[0].site_loads_.size();
    seps_.resize(site_count, {0, 0});
    is_always_empty_.resize(site_count, false);
    for (size_t site_idx = 0; site_idx < site_count; site_idx++) {
        // load, day
        vector<pair<int, size_t>> arr;
        arr.clear();
        arr.reserve(days_result_.size());
        for (size_t day = 0; day < days_result_.size(); day++) {
            arr.push_back({days_result_[day].site_loads_[site_idx], day});
        }
        std::sort(arr.begin(), arr.end(),
                  [](const pair<int, size_t> &l, const pair<int, size_t> &r) {
                      return l.first < r.first;
                  });
        size_t sep_idx = ceil(arr.size() * 0.95) - 1;
        seps_[site_idx] = arr[sep_idx];
        if (seps_[site_idx].first > base_) {
            printf("site %ld loads:\n", site_idx);
            for (auto &e : arr) {
                printf("<%d,%ld> ", e.first, e.second);
            }
            printf("\n");
        }
    }

}
