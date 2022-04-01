#pragma once

#include <algorithm>
#include <list>
#include <string>
#include <vector>
#include <cmath>

#include "client.hpp"
#include "site.hpp"

using namespace std;

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
        for (const auto &site : sites) {
            site_loads_.push_back(site.GetAllocatedBandwidth());
        }
    }
    size_t GetClientAccessibleSiteCount(size_t C) const {
        return cli_tbls_[C].tbl.size();
    }
    const list<pair<string, int>> &GetAllocationTable(size_t C,
                                                      size_t S) const {
        return cli_tbls_[C].tbl[S];
    }

  private:
    vector<AllocationTable> cli_tbls_;
    vector<int> site_loads_;
};

using ResultSetIter = std::vector<Result>::iterator;

// 所有天的客户分配情况
class ResultSet {
  public:
    ResultSet() = default;
    ResultSet(const std::vector<Site> &sites, int base) : base_(base) {
        sites_caps_.reserve(sites.size());
        for (const auto &site : sites) {
            sites_caps_.push_back(site.GetTotalBandwidth());
        }
    }
    void Reserve(size_t n) { days_result_.reserve(n); }
    void AddResult(Result &&day_res) { days_result_.push_back(day_res); }
    ResultSetIter begin() { return days_result_.begin(); }
    ResultSetIter end() { return days_result_.end(); }
    void ComputeAllSeps() {
        assert(not days_result_.empty());
        size_t site_count = days_result_[0].site_loads_.size();
        seps_95_.resize(site_count, 0);
        is_always_empty_.resize(site_count, false);
        for (size_t S = 0; S < site_count; S++) {
            vector<int> arr;
            arr.clear();
            arr.reserve(days_result_.size());
            for (auto &day_res : days_result_) {
                arr.push_back(day_res.site_loads_[S]);
            }
            std::sort(arr.begin(), arr.end());
            seps_95_[S] = arr[static_cast<size_t>(arr.size() * 0.95)];
            if (arr.back() == 0) {
                is_always_empty_[S] = true;
            }
        }
    }
    int GetGrade() {
        int grade = 0;
        printf("all 95 seperators:\n");
        for (auto &sep : seps_95_) {
            printf("%d ", sep);
        }
        printf("\n");
        for (size_t S = 0; S < seps_95_.size(); S++) {
            if (is_always_empty_[S]) {
                continue;
            }
            if (seps_95_[S] <= base_) {
                grade += base_;
            } else {
                grade += static_cast<int>(pow(1.0 * (seps_95_[S] - base_), 2) / sites_caps_[S] + seps_95_[S]);
            }
        }
        return grade;
    }

  private:
    std::vector<bool> is_always_empty_;
    std::vector<Result> days_result_;
    std::vector<int> seps_95_;
    std::vector<int> sites_caps_;
    int base_{0};
};
