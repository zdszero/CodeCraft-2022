#pragma once

#include <list>
#include <string>
#include <vector>

#include "client.hpp"
#include "site.hpp"

using namespace std;

// 一天中所有客户的分配情况
class Result {
  public:
    Result() = default;
    explicit Result(const vector<Client> &clients, const vector<Site> &sites) {
        cli_tbls_.reserve(clients.size());
        for (const auto &cli : clients) {
            cli_tbls_.push_back(cli.GetAllocationTable());
        }
        site_loads_.reserve(sites.size());
        for (const auto &site : sites) {
            site_loads_.push_back(site.GetRemainBandwidth());
        }
    }
    size_t GetClientAccessibleSiteCount(size_t C) const {
        return cli_tbls_[C].tbl.size();
    }
    const list<pair<string, int>> &GetAllocationTable(size_t C, size_t S) const {
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
    void Reserve(size_t n) { days_result_.reserve(n); }
    void AddResult(Result &&day_res) { days_result_.push_back(day_res); }
    ResultSetIter begin() { return days_result_.begin(); }
    ResultSetIter end() { return days_result_.end(); }

  private:
    std::vector<Result> days_result_;
};
