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
    vector<vector<int>> demandsByClient;
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
    void Schedule(Demand &d, int day);
    // 贪心将可以分配满的site先分配满
    void GreedyAllocate(Demand &d, int day);
    // 分配到base cost上下
    void BaseAllocate(Demand &d);
    // 平均分
    void AverageAllocate(Demand &d);
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

    for (int i = 0; i < demands_.size(); i++) {
        auto d = demands_[i];
        demandsByClient.push_back(vector<int>(clients_.size(), 0));
        for (int j = 0; j < clients_.size(); j++) {
            demandsByClient[i][j] = d.GetClientDemand(j);
        }
    }

}

struct tem {
    bool operator()(Daily_site a, Daily_site b) {
        return a.GetTotal() < b.GetTotal();
    }
};

void SystemManager::Process() {
    std::vector<int> max_site_idx;
    std::vector<int> Day_idx;
    for (int i = 0; i < sites_.size(); i++) {
        max_site_idx.push_back(i);
        sites_[i].SetSeperateBandwidth(sites_[i].GetTotalBandwidth()*0.07);
    }
    for (int i = 0; i < demands_.size(); i++) {
        Day_idx.push_back(i);
    }
    // Sort by site capacity
    auto sites_copy = sites_;
    for(int ii = 0; ii < sites_.size(); ii++) {
        for(int jj = sites_.size() - 1; jj > 0; jj--) {
            if(sites_copy[jj].GetTotalBandwidth() > sites_copy[jj-1].GetTotalBandwidth()) {
                std::swap(sites_copy[jj], sites_copy[jj-1]);
                std::swap(max_site_idx[jj], max_site_idx[jj-1]);
            }
        }
    }

    // 按照site容量降序计算在哪一天打满
    auto demands_copy = demandsByClient;
    daily_full_site_idx.clear();
    for (int demands_idx = 0; demands_idx < demands_.size();
         demands_idx++) {
        daily_full_site_idx.push_back(std::vector<int>());
    }
    for (int site_index : max_site_idx) {
        std::priority_queue<Daily_site, std::vector<Daily_site>, tem>
                site_max_req;
        for (int demands_idx = 0; demands_idx < demands_copy.size();
             demands_idx++) {
            auto &demand = demands_copy[demands_idx];
            int cur_sum = 0;
            for (int cli_idex : sites_[site_index].GetRefClients()) {
                cur_sum += demand[cli_idex] / clients_[cli_idex].GetSiteCount();
                //((sites_[site_index].GetTotalBandwidth() * 1.0) /
                //(1.0 * clients_[cli_idex].GetAccessTotal()));
            }
            if (cur_sum > 0) {
                site_max_req.push({demands_idx, site_index, cur_sum-sites_[site_index].GetSeperateBandwidth(),
                                   sites_[site_index].GetTotalBandwidth()});
            }
        }
        for (int j = 0; j < (int)(demands_.size() * 0.05); j++) {
            if (site_max_req.empty()) {
                break;
            }
            Daily_site daily_site = site_max_req.top();
            int cur_time = daily_site.GetTime();
            int remainBandwidth = daily_site.GetRemainBandwidth();
            auto &demand = demands_copy[cur_time];
            for (int c : sites_[daily_site.GetSiteIdx()].GetRefClients()) {
                if (demand[c] == 0) {
                    continue;
                }
                int allocated = std::min(remainBandwidth, demand[c]);
                // assert(clients_[c].SetAllocationBySite(max_site_idx,
                // allocated));
                demand[c] -= allocated;
                remainBandwidth -= allocated;
                if (remainBandwidth == 0) {
                    break;
                }
            }
            daily_full_site_idx[cur_time].push_back(
                    daily_site.GetSiteIdx());
            site_max_req.pop();
        }
    }
    for (int day_idx : Day_idx) {
        auto &d = demands_[day_idx];
        Schedule(d, day_idx);
    }
}

void SystemManager::Schedule(Demand &d, int day) {
    // 重设所有server的剩余流量
    for (auto &site : sites_) {
        site.Reset();
    }
    for (auto &client : clients_) {
        client.Reset();
    }
    GreedyAllocate(d, day);
    BaseAllocate(d);
    AverageAllocate(d);

    // update sites seperate value
    for (auto &site : sites_) {
        site.ResetSeperateBandwidth();
    }
    WriteSchedule(GenerateResult());
}

void SystemManager::GreedyAllocate(Demand &d, int day) {
    auto &need = d.GetStreamDemands();
    if(daily_full_site_idx[day].empty()) {
        return;
    }
    for(int max_site_idx : daily_full_site_idx[day]) {
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
        site.SetFullThisTime();
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
            int sep = 0;
            int allocated = 0;
            auto &cli = clients_[C];
            auto &site_indexes = cli.GetAccessibleSite();
            for (size_t S = 0; S < site_indexes.size(); S++) {
                size_t site_idx = site_indexes[S];
                auto &site = sites_[site_idx];
                if(site.IsFullThisTime()) {
                    continue;
                }
                sep = site.GetSeperateBandwidth();
                allocated = site.GetAllocatedBandwidth();
                if(allocated >= sep - v[C]) {
                    continue;
                }
                if (site.GetRemainBandwidth() >= v[C]) {
                    site.DecreaseBandwith(v[C]);
                    cli.AddAllocation(S, stream_name);
                    v[C] = 0;
                    break;
                }
            }
            //assert(flag == true);
        }
    }
}

void SystemManager::AverageAllocate(Demand &d) {
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
            int min_site = -1;
            int min_S = -1;
            double min_value = 9999999999;
            double grade = 0;
            int used = 0;
            int sep = 0;
            for (size_t S = 0; S < site_indexes.size(); S++) {
                size_t site_idx = site_indexes[S];
                auto &site = sites_[site_idx];
                if(site.GetRemainBandwidth() >= v[C]) {
                    used = site.GetAllocatedBandwidth() + v[C];
                    sep = site.GetSeperateBandwidth();
                    if(used <= sep) {
                        min_site = site_idx;
                        min_S = S;
                        min_value = grade;
                        flag = true;
                        break;
                    }
                    grade = (used - base_cost_)*(used - base_cost_) * 1.0 / site.GetTotalBandwidth() * 1.0;
                    if(grade < min_value) {
                        min_site = site_idx;
                        min_S = S;
                        min_value = grade;
                        flag = true;
                    }
                }
            }
            auto &site = sites_[min_site];
            site.DecreaseBandwith(v[C]);
            site.ResetSeperateBandwidth();
            cli.AddAllocation(min_S, stream_name);
            v[C] = 0;
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
