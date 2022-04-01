#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>

#include "daily_site.hpp"
#include "file_parser.hpp"

using namespace std;

using allocation_t = vector<vector<list<string>>>;
using all_demand_t = vector<Demand>;

bool is_valid = true;

class SystemManager {
  public:
    SystemManager() = default;
    SystemManager(const string &output_filename) {
        output_fp_ = fopen(output_filename.c_str(), "w");
    }
    ~SystemManager() {
        if (output_fp_) {
            fclose(output_fp_);
        }
    }
    // 初始化系统模块
    void Init();
    // 不断读取时间戳的请求并且处理
    void Process();

  private:
    FILE *output_fp_{stdout};
    FileParser file_parser_;
    int qos_constraint_;
    int base_cost_;
    vector<Site> sites_;
    vector<Client> clients_;
    vector<Demand> demands_; // demands all mtimes
    long total_demand_{0};
    long avg_demand_{0};
    long max_demand_{0};
    long mid_demand_{0};
    long over_demand_all_{0};
    int all_full_times_;
    vector<allocation_t> results_;
    vector<vector<int>> daily_full_site_idx;
    vector<vector<int>> best_daily_full_site_idx;

    // 根据服务器的当前容量，以及被访问次数，动态计算流量分配ratio
    bool SetClientRatio(int i, const vector<int> &demand);
    // 对于每一个时间戳的请求进行调度
    void Schedule(Demand &d);
    // 贪心将可以分配满的site先分配满
    void GreedyAllocate(Demand &d);
    // 分配到base cost上下
    void BaseAllocate(Demand &d);
    // 获取第i个client的第j个边缘结点
    Site &GetSite(int i, int j) { return sites_[clients_[i].GetSiteIndex(j)]; }
    // 向/output/solution.txt中写出结果
    void WriteSchedule(const allocation_t &res);
    // 根据函数计算当前应该打满次数
    int GetFullTimes(const Demand &d);
    // 从clients中提取allocation table
    allocation_t GenerateResult();
    // 获取成绩
    long GetGrade();
};

void SystemManager::Init() {
    file_parser_.ParseSites(sites_);
    file_parser_.ParseConfig(qos_constraint_, base_cost_);
    file_parser_.ParseQOS(clients_, qos_constraint_);
    while (file_parser_.ParseDemand(clients_.size(), demands_));
    // 计算每个site被多少client使用
    for (size_t i = 0; i < clients_.size(); i++) {
        for (const auto site_idx : clients_[i].GetAccessibleSite()) {
            sites_[site_idx].AddRefClient(i);
        }
    }
    // 将client中的accessible sites根据引用次数排序，并且计算client的总需求量
    std::for_each(clients_.begin(), clients_.end(), [this](Client &cli) {
        sort(cli.GetAccessibleSite().begin(),
             cli.GetAccessibleSite().end(), [this](int l, int r) {
                 return sites_[l].GetRefTimes() > sites_[r].GetRefTimes();
             });

        for (auto site : cli.GetAccessibleSite()) {
            cli.AddAccessTotal(sites_[site].GetTotalBandwidth());
        }
    });
    // set variables
    for (const auto &d : demands_) {
        long cur_total = d.GetTotalDemand();
        if (cur_total > max_demand_) {
            max_demand_ = cur_total;
        }
        total_demand_ += cur_total;
    }
    avg_demand_ = total_demand_ / demands_.size();
    // all_full_times_ = static_cast<int>(demands_.size() * 0.05 * sites_.size());
    for (auto &site : sites_) {
        site.SetMaxFullTimes(demands_.size() / 20 - 1);
    }
}

void SystemManager::Process() {
    for (auto &d : demands_) {
        Schedule(d);
    }
}

void SystemManager::Schedule(Demand &d) {
    // 重设所有server的剩余流量
    for (auto &site : sites_) {
        site.Reset();
    }
    for (auto &client : clients_) {
        client.Reset();
    }
    /* GreedyAllocate(d); */
    BaseAllocate(d);
    WriteSchedule(GenerateResult());
}

void SystemManager::GreedyAllocate(Demand &d) {
    auto &need = d.GetStreamDemands();
    int full_times = sites_.size() / 20;
    for (int F = 0; F < full_times; F++) {
        int max_ = numeric_limits<int>::min();
        int max_site_idx = -1;
        for (size_t S = 0; S < sites_.size(); S++) {
            int cur_ = 0;
            auto &site = sites_[S];
            if (!site.IsSafe()) {
                continue;
            }
            for (size_t C : site.GetRefClients()) {
                cur_ += d.GetClientDemand(C);
            }
            if (min(cur_, site.GetRemainBandwidth()) > max_) {
                max_ = cur_;
                max_site_idx = static_cast<int>(S);
            }
        }
        if (max_site_idx == -1) {
            return;
        }
        auto &site = sites_[max_site_idx];
        for (size_t C : site.GetRefClients()) {
            for (auto it = need.begin(); it != need.end(); it++) {
                if (it->second[C] > site.GetRemainBandwidth()) {
                    continue;
                }
                if (it->second[C] == 0) {
                    continue;
                }
                site.DecreaseBandwith(it->second[C]);
                it->second[C] = 0;
                clients_[C].AddAllocationBySiteIndex(max_site_idx, it->first);
            }
        }
        site.IncFullTimes();
    }
}

void SystemManager::BaseAllocate(Demand &d) {
    auto &need = d.GetStreamDemands();
    for (auto it = need.begin(); it != need.end(); it++) {
        string stream_name = it->first;
        auto &v = it->second;
        // client idx i
        for (size_t C = 0; C < v.size(); C++) {
            if (v[C] == 0) {
                continue;
            }
            auto &cli = clients_[C];
            auto &site_indexes = cli.GetAccessibleSite();
            bool flag = false;
            for (size_t S = 0; S < site_indexes.size(); S++) {
                size_t site_idx = site_indexes[S];
                auto &site = sites_[site_idx];
                if (site.GetRemainBandwidth() >= v[C]) {
                    site.DecreaseBandwith(v[C]);
                    cli.AddAllocation(S, stream_name);
                    v[C] = 0;
                    flag = true;
                    break;
                }
            }
            assert(flag == true);
        }
    }
}

allocation_t SystemManager::GenerateResult() {
    allocation_t v;
    v.reserve(clients_.size());
    for (const auto &c : clients_) {
        v.push_back(c.GetAllocationTable());
    }
    return v;
}

void SystemManager::WriteSchedule(const allocation_t &res) {
    // for each client index i
    for (size_t i = 0; i < res.size(); i++) {
        fprintf(output_fp_, "%s:", clients_[i].GetName());
        bool flag = false;
        // for each accessible server j
        for (size_t j = 0; j < res[i].size(); j++) {
            const auto &allocate_list = res[i][j];
            int site_idx = clients_[i].GetSiteIndex(j);
            if (!allocate_list.empty()) {
                if (flag) {
                    fprintf(output_fp_, ",");
                }
                fprintf(output_fp_, "<%s", sites_[site_idx].GetName());
                for (string s : allocate_list) {
                    fprintf(output_fp_, ",%s", s.c_str());
                }
                fprintf(output_fp_, ">");
                flag = true;
            }
        }
        fprintf(output_fp_, "\n");
    }
}

int main() {
    /* SystemManager manager; */
    SystemManager manager("/output/solution.txt");
    manager.Init();
    manager.Process();
}
