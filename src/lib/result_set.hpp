#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <set>
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
    explicit Result(size_t day, const vector<Client> &clients, const vector<Site> &sites) : day_(day) {
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
    size_t GetClientAccessibleSiteCount(size_t C) const { return cli_tbls_[C].tbl.size(); }
    const list<Stream> &GetAllocationTable(size_t C, size_t S) const { return cli_tbls_[C].tbl[S]; }
    // migrate streams from server[From] to other accessible servers
    int Migrate(size_t from, vector<Site> *sites, vector<pair<int, size_t>> &seps, int base, int base_cost, int day,
                bool isSep, vector<int> &max_acc) {
        int cur_load = site_loads_[from];
        vector<int> moved(max_acc.size(), 0);

        for (auto it = site_streams_[from].begin(); it != site_streams_[from].end();) {
            assert(it->site_idx == from);
            // which client is the stream from
            auto &cli_ref = sites->at(it->cli_idx).GetRefClients();
            int To = -1;
            int min_free = numeric_limits<int>::max();
            /* int max_dec_cost = 0; */
            // choose To server to move
            for (size_t candidate : cli_ref) {
                if (candidate == from) {
                    continue;
                }
                if (moved[candidate] + it->stream_size > max_acc[candidate]) {
                    continue;
                }
                // if server[To] used size is less than factor * cap
                int free = seps[candidate].first - site_loads_[candidate] - it->stream_size;
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
            moved[To] += it->stream_size;
            cur_load -= it->stream_size;
            it = MoveStream(it, from, To);
            if (cur_load <= base) {
                // ret = true;
                break;
            }
        }
        return cur_load;
    }
    int ExpelTop5Test(size_t from, int base, vector<pair<int, size_t>> &seps, vector<Client> *clis,
                      const vector<int> &max_acc) {
        int cur_load = site_loads_[from];
        vector<int> all_moved(max_acc.size(), 0);
        for (auto it = site_streams_[from].begin(); it != site_streams_[from].end();) {
            assert(it->site_idx == from);
            // which client is the stream from
            auto &cli_ref = clis->at(it->cli_idx).GetAccessibleSite();
            int to = -1;
            // choose To server to move
            for (size_t candidate : cli_ref) {
                if (candidate == from) {
                    continue;
                }
                if (all_moved[candidate] + it->stream_size >= max_acc[candidate]) {
                    continue;
                }
                if (site_loads_[candidate] + it->stream_size <= seps[candidate].first) {
                    to = candidate;
                    break;
                }
            }
            // if all other site's load is greater
            if (to == -1) {
                it++;
                continue;
            }
            cur_load -= it->stream_size;
            all_moved[to] += it->stream_size;
            if (cur_load <= base) {
                break;
            }
        }
        return cur_load;
    }

    void ExpelTop5(size_t from, int base, vector<pair<int, size_t>> &seps, vector<Client> *clis,
                   const vector<int> &max_acc) {
        vector<int> all_moved(max_acc.size(), 0);
        for (auto it = site_streams_[from].begin(); it != site_streams_[from].end();) {
            assert(it->site_idx == from);
            // which client is the stream from
            auto &cli_ref = clis->at(it->cli_idx).GetAccessibleSite();
            int to = -1;
            // choose To server to move
            for (size_t candidate : cli_ref) {
                if (candidate == from) {
                    continue;
                }
                if (all_moved[candidate] + it->stream_size >= max_acc[candidate]) {
                    continue;
                }
                if (site_loads_[candidate] + it->stream_size <= seps[candidate].first) {
                    to = candidate;
                    break;
                }
            }
            // if all other site's load is greater
            if (to == -1) {
                it++;
                continue;
            }
            all_moved[to] += it->stream_size;
            it = MoveStream(it, from, to);
            if (site_loads_[from] <= base /*|| site_loads_[from] <= seps[from].first * 0.2*/) {
                break;
            }
        }
        // for (size_t i = 0; i < all_moved.size(); i++) {
        //     if (all_moved[i] > 0)
        //         printf("%d ", all_moved[i]);
        // }
        // printf("\n");
    }

    void UpdateTop5(size_t to, int base, vector<pair<int, size_t>> &seps, vector<vector<size_t>> &cli_refs,
                    vector<vector<size_t>> &site_refs, vector<int> &caps) {
        for (size_t cli_idx : site_refs[to]) {
            for (size_t from : cli_refs[cli_idx]) {
                if (from == to) {
                    continue;
                }
                if (site_loads_[from] <= base) {
                    continue;
                }
                if (site_loads_[from] > seps[from].first) {
                    continue;
                }
                auto &streams = cli_tbls_[cli_idx].GetList(from);
                for (auto it = streams.begin(); it != streams.end();) {
                    /* printf("site idx: %ld, from: %ld\n", str.site_idx, from);
                     */
                    assert(it->site_idx == from);
                    if (site_loads_[to] + it->stream_size <= caps[to]) {
                        auto next_it = next(it, 1);
                        Stream tmps = *it;
                        auto sit = site_streams_[from].begin();
                        while (sit != site_streams_[from].end()) {
                            if (*sit == tmps) {
                                break;
                            }
                            sit++;
                        }
                        assert(sit != site_streams_[from].end());
                        MoveStream(sit, from, to);
                        it = next_it;
                    }
                    if (site_loads_[to] == caps[to]) {
                        goto End;
                    }
                    it++;
                }
            }
        }
    End:;
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
    using ResultSetIter = vector<Result>::iterator;
    enum class ComputeJob { GET_GRADE, GET_95, GET_5 };

  public:
    ResultSet() = default;
    ResultSet(vector<Site> &sites, vector<Client> &clis, int base) : base_(base) {
        clis_ = &clis;
        sites_ = &sites;
    }
    void Migrate();
    void AdjustTop5();
    void Reserve(size_t n) { days_result_.reserve(n); }
    void Resize(size_t n) { days_result_.resize(n); }
    void AddResult(Result &&day_res) { days_result_.push_back(day_res); }
    void SetResult(size_t day, Result &&day_res) { days_result_[day] = move(day_res); }
    int GetGrade();
    ResultSetIter begin() { return days_result_.begin(); }
    ResultSetIter end() { return days_result_.end(); }

    void PrintLoads(bool all = false);

  private:
    vector<Result> days_result_;
    vector<Site> *sites_;
    vector<Client> *clis_;
    // 没一台服务器，当前的<95分位值，对应的天>
    vector<pair<int, size_t>> seps_;
    // 每一个服务器，需要迁移的所有<流量大小，对应的天>
    vector<list<pair<int, size_t>>> site_migrate_days_;
    // 从95分位值 到 FACTOR * 95分位值
    vector<list<pair<int, size_t>>> site_top5_days_;
    vector<bool> is_always_empty_;
    vector<int> site_cnt_;
    int base_{0};

    void ComputeAllSeps(ComputeJob job);
    void ComputeSomeSeps(ComputeJob job, size_t site_idx);
};

inline int ResultSet::GetGrade() {
    ComputeAllSeps(ComputeJob::GET_5);
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
    int zero_count = 0;
    for (size_t S = 0; S < seps_.size(); S++) {
        if (is_always_empty_[S]) {
            zero_count++;
            continue;
        }
        total += seps_[S].first;
        // if (seps_[S].first == 0) {
        if (seps_[S].first <= base_) {
            if (seps_[S].first > 0)
                grade += base_;
        } else {
            grade += static_cast<int>(pow(1.0 * (seps_[S].first - base_), 2) / sites_->at(S).GetTotalBandwidth() +
                                      seps_[S].first);
        }
    }
    printf("zeros: %d\n", zero_count);
    printf("total: %d\n", total);
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
    //    sort(site_indexes.begin(), site_indexes.end(), [this](size_t l, size_t r) {
    //        return sites_->at(l).GetTotalBandwidth() < sites_caps_[r];
    //    });

    for (auto site_idx : site_indexes) {
        auto &site_mig_day = site_migrate_days_[site_idx];
        int base = base_; /*max(base_, (int)(seps_[site_idx].first * 0.5)); */ //
        int cur_used = 0;
        int sep_day = -1;
        bool isSep = true;
        for (auto it = site_mig_day.begin(); it != site_mig_day.end();) {
            size_t day = it->second;
            if (it->first <= base) {
                break;
            }

            vector<int> max_accept(sites_->size(), numeric_limits<int>::max());
            for (int N = 1; N <= 3; N++) {
                if (day + N >= days_result_.size())
                    break;
                auto &next_res = days_result_[day + N];
                int P = 1;
                for (size_t k = 0; k < N; k++) {
                    P *= 20;
                }
                for (size_t site_idx = 0; site_idx < sites_->size(); site_idx++) {
                    assert(next_res.site_loads_[site_idx] <= sites_->at(site_idx).GetTotalBandwidth());
                    max_accept[site_idx] = min(max_accept[site_idx], P * (sites_->at(site_idx).GetTotalBandwidth() -
                                                                          next_res.site_loads_[site_idx]));
                }
            }
            auto origin_loads = days_result_[day].site_loads_;
            cur_used = days_result_[day].Migrate(site_idx, sites_, seps_, base, base_, day, isSep, max_accept);
            auto cur_loads = days_result_[day].site_loads_;
            for (size_t site_idx = 0; site_idx < origin_loads.size(); site_idx++) {
                int change = cur_loads[site_idx] - origin_loads[site_idx];
                // if (change < -10000) {
                //     printf("idx = %ld, origin = %d, cur = %d\n", site_idx, origin_loads[site_idx],
                //     cur_loads[site_idx]); printf("change = %d\n", change);
                // }
                for (int N = 1; N <= 3; N++) {
                    if (day + N >= days_result_.size())
                        break;
                    auto &next_res = days_result_[day + N];
                    int P = 1;
                    for (size_t k = 0; k < N; k++) {
                        P *= 20;
                    }
                    next_res.site_loads_[site_idx] += (change / P);
                    assert(next_res.site_loads_[site_idx] <= sites_->at(site_idx).GetTotalBandwidth());
                }
            }
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

inline void ResultSet::AdjustTop5() {
    ComputeAllSeps(ComputeJob::GET_5);
    vector<int> sites_idx(site_top5_days_.size(), 0);
    for (int i = 0; i < site_top5_days_.size(); i++) {
        sites_idx.push_back(i);
    }
    sort(sites_idx.begin(), sites_idx.end(), [this](size_t l, size_t r) { return site_cnt_[l] < site_cnt_[r]; });
    for (int i = 0; i < sites_idx.size() / 2; i++) {
        int site_idx = sites_idx[i];
        //    }
        //    for (size_t site_idx = 0; site_idx < site_top5_days_.size(); site_idx++) {
        for (auto &p : site_top5_days_[site_idx]) {
            vector<int> max_accept(sites_->size(), numeric_limits<int>::max());
            size_t day = p.second;
            for (int N = 1; N <= 3; N++) {
                if (day + N >= days_result_.size())
                    break;
                auto &next_res = days_result_[day + N];
                int P = 1;
                for (size_t k = 0; k < N; k++) {
                    P *= 20;
                }
                for (size_t site_idx = 0; site_idx < sites_->size(); site_idx++) {
                    assert(next_res.site_loads_[site_idx] <= sites_->at(site_idx).GetTotalBandwidth());
                    max_accept[site_idx] = min(max_accept[site_idx], P * (sites_->at(site_idx).GetTotalBandwidth() -
                                                                          next_res.site_loads_[site_idx]));
                }
            }
            // for (auto acc : max_accept) {
            //     if (acc < 18888) {
            //         printf("%d ", acc);
            //     }
            // }
            // printf("\n");
            auto origin_loads = days_result_[day].site_loads_;
            if (days_result_[day].ExpelTop5Test(site_idx, base_, seps_, clis_, max_accept) >
                seps_[site_idx].first) {
                continue;
            }
            days_result_[day].ExpelTop5(site_idx, base_, seps_, clis_, max_accept);
            auto cur_loads = days_result_[day].site_loads_;
            for (size_t site_idx = 0; site_idx < origin_loads.size(); site_idx++) {
                int change = cur_loads[site_idx] - origin_loads[site_idx];
                // if (change < -10000) {
                //     printf("idx = %ld, origin = %d, cur = %d\n", site_idx, origin_loads[site_idx],
                //     cur_loads[site_idx]); printf("change = %d\n", change);
                // }
                for (int N = 1; N <= 3; N++) {
                    if (day + N >= days_result_.size())
                        break;
                    auto &next_res = days_result_[day + N];
                    int P = 1;
                    for (size_t k = 0; k < N; k++) {
                        P *= 20;
                    }
                    next_res.site_loads_[site_idx] += (change / P);
                    assert(next_res.site_loads_[site_idx] <= sites_->at(site_idx).GetTotalBandwidth());
                }
            }
        }
        // ComputeSomeSeps(ComputeJob::GET_5, site_idx);
        // for (auto &p : site_top5_days_[site_idx]) {
        //     size_t day = p.second;
        //     days_result_[day].UpdateTop5(site_idx, base_, seps_, cli_ref_sites_idx_,
        //                                  site_refs_, sites_caps_);
        // }
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
    site_cnt_ = vector<int>(site_count, 0);
    for (size_t site_idx = 0; site_idx < site_count; site_idx++) {
        // load, day
        vector<pair<int, size_t>> arr;
        arr.clear();
        arr.reserve(days_result_.size());
        for (size_t day = 0; day < days_result_.size(); day++) {
            arr.push_back({days_result_[day].site_loads_[site_idx], day});
        }
        sort(arr.begin(), arr.end(),
             [](const pair<int, size_t> &l, const pair<int, size_t> &r) { return l.first < r.first; });
        size_t sep_idx = ceil(arr.size() * 0.95) - 1;
        seps_[site_idx] = arr[sep_idx];
        if (arr.back().first == 0) {
            is_always_empty_[site_idx] = true;
        }
        // get migrate days

        if (job == ComputeJob::GET_95) {
            if (is_always_empty_[site_idx]) {
                continue;
            }
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
            if (is_always_empty_[site_idx]) {
                continue;
            }
            int cnt = 0;
            for (int i = static_cast<int>(sep_idx); i >= 0; i--) {
                if (arr[sep_idx].first - arr[i].first < base_ / 2) {
                    cnt++;
                } else {
                    break;
                }
            }
            site_cnt_[site_idx] = cnt;
            //            if (cnt >= arr.size() - sep_idx) {
            //                continue;
            //            }
            /* printf("site idx %ld: ", site_idx); */
            for (size_t i = sep_idx + 1; i < arr.size(); i++) {
                /* printf("<%d, %ld> ", arr[i].first, arr[i].second); */
                site_top5_days_[site_idx].push_back(arr[i]);
            }
            /* printf("\n"); */
        }
    }
}

inline void ResultSet::ComputeSomeSeps(ComputeJob job, size_t site_idx) {
    if (job == ComputeJob::GET_95) {
        site_migrate_days_[site_idx].clear();
    } else if (job == ComputeJob::GET_5) {
        site_top5_days_[site_idx].clear();
    }
    is_always_empty_[site_idx] = false;
    vector<pair<int, size_t>> arr;
    arr.clear();
    arr.reserve(days_result_.size());
    for (size_t day = 0; day < days_result_.size(); day++) {
        arr.push_back({days_result_[day].site_loads_[site_idx], day});
    }
    sort(arr.begin(), arr.end(),
         [](const pair<int, size_t> &l, const pair<int, size_t> &r) { return l.first < r.first; });
    size_t sep_idx = ceil(arr.size() * 0.95) - 1;
    seps_[site_idx] = arr[sep_idx];
    if (arr.back().first == 0) {
        is_always_empty_[site_idx] = true;
    }
    // get migrate days
    if (job == ComputeJob::GET_95) {
        if (is_always_empty_[site_idx]) {
            return;
        }
        if (static_cast<int>(arr[sep_idx].first) <= base_) {
            return;
        }
        for (int i = static_cast<int>(sep_idx); i >= 0; i--) {
            if (arr[i].first <= base_) {
                break;
            }
            site_migrate_days_[site_idx].push_back(arr[i]);
        }
    } else if (job == ComputeJob::GET_5) {
        if (is_always_empty_[site_idx]) {
            return;
        }
        /* printf("site idx %ld: ", site_idx); */
        for (size_t i = sep_idx + 1; i < arr.size(); i++) {
            /* printf("<%d, %ld> ", arr[i].first, arr[i].second); */
            site_top5_days_[site_idx].push_back(arr[i]);
        }
        /* printf("\n"); */
    }
}

inline void ResultSet::PrintLoads(bool all) {
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
        sort(arr.begin(), arr.end(),
             [](const pair<int, size_t> &l, const pair<int, size_t> &r) { return l.first < r.first; });
        size_t sep_idx = ceil(arr.size() * 0.95) - 1;
        seps_[site_idx] = arr[sep_idx];
        if (seps_[site_idx].first > base_) {
            if (all) {
                printf("site %ld loads:\n", site_idx);
                for (auto &e : arr) {
                    printf("<%d,%ld> ", e.first, e.second);
                }
            } else {
                printf("%3ld %2s -- %d: ", site_idx, sites_->at(site_idx).GetName(), sites_->at(site_idx).GetRefTimes());
                auto &esep = arr[sep_idx];
                auto &e1 = arr[sep_idx + 1];
                auto &e2 = arr[arr.size() - 1];
                printf("sep: <%5d,%5ld>   min: <%5d,%5ld>   max: <%5d,%5ld>", esep.first, esep.second, e1.first,
                       e1.second, e2.first, e2.second);
            }
            printf("\n");
        }
    }
}
