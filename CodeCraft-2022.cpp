#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <list>
#include <memory>
#include <numeric>

#include "file_parser.hpp"

#define ll long long

template <typename T> void print_vec(std::vector<T> &v) {
    for (T val : v) {
        std::cout << val << " ";
    }
    std::cout << std::endl;
}

class SystemManager {
  public:
    SystemManager() = default;
    SystemManager(const std::string &output_filename) {
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
    std::vector<Site> sites_;
    std::vector<Client> clients_;
    std::vector<std::vector<int>> demands_;
    ll total_demand_{0};
    ll avg_demand_{0};
    ll max_demand_{0};
    ll mid_demand_{0};
    ll over_demand_all_{0};
    int all_full_times_;

    // 根据服务器的当前容量，以及被访问次数，动态计算流量分配ratio
    void SetClientRatio(int i, const std::vector<int> &demand);
    // 对于每一个时间戳的请求进行调度
    void Schedule(std::vector<int> &demand);
    // 贪心将可以分配满的site先分配满
    void GreedyAllocate(std::vector<int> &demand);
    // 平均分配
    void AverageAllocate(std::vector<int> &demand);
    // 获取第i个client的第j个边缘结点
    Site &GetSite(int i, int j) { return sites_[clients_[i].GetSiteIndex(j)]; }
    // 获取所有的请求
    void ReadDemands();
    // 向/output/solution.txt中写出结果
    void WriteSchedule();
    // 计算site当前被引用的需求总和
    ll GetSiteTotalDemand(const Site &site, const std::vector<int> &demand);
    // 根据函数计算当前应该打满次数
    int GetFullTimes(const std::vector<int> &demand);
};

void SystemManager::Init() {
    file_parser_.ParseSites(sites_);
    file_parser_.ParseConfig(qos_constraint_);
    file_parser_.ParseQOS(clients_, qos_constraint_);
    // 计算每个site被多少client使用
    for (size_t i = 0; i < clients_.size(); i++) {
        for (const auto site_idx : clients_[i].GetAccessibleSite()) {
            sites_[site_idx].AddRefClient(i);
        }
    }
    ReadDemands();
}

void SystemManager::Process() {
    /* std::vector<int> demand; */
    /* while (file_parser_.ParseDemand(clients_.size(), demand)) { */
    /*     Schedule(demand); */
    /* } */
    for (auto &demand : demands_) {
        Schedule(demand);
    }
    // print full times
    printf("full times: ");
    for (size_t i = 0; i < sites_.size(); i++) {
        printf("ref count: %d, full times: %d\n", sites_[i].GetRefTimes(),
               sites_[i].GetFullTimes());
    }
}

void SystemManager::SetClientRatio(int i, const std::vector<int> &demand) {
    auto &client = clients_[i];
    double sum = 0;
    std::vector<double> inter_values(client.GetSiteCount());
    for (size_t j = 0; j < client.GetSiteCount(); j++) {
        const auto &site = GetSite(i, j);
        inter_values[j] =
            (1.0 / GetSiteTotalDemand(site, demand) * site.GetRemainBandwidth() / site.GetTotalBandwidth());
        sum += inter_values[j];
    }
    // 当前client的所有可以访问的节点都没有剩余容量了，寄！
    assert(sum != 0);
    for (size_t j = 0; j < client.GetSiteCount(); j++) {
        client.SetRatio(j, inter_values[j] / sum);
    }
}


ll SystemManager::GetSiteTotalDemand(const Site &site, const std::vector<int> &demand) {
    ll res = 0;
    for (int C : site.GetRefClients()) {
        res += demand[C];
    }
    return res;
}

void SystemManager::Schedule(std::vector<int> &demand) {
    // 重设所有server的剩余流量
    for (auto &site : sites_) {
        site.ResetRemainBandwidth();
    }
    for (auto &client : clients_) {
        client.Reset();
    }
    auto demand_cpy = demand;
    GreedyAllocate(demand_cpy);
    for (size_t i = 0; i < clients_.size(); i++) {
        assert(clients_[i].GetTotalAllocation() <= demand[i]);
    }
    AverageAllocate(demand_cpy);
    for (int d : demand_cpy) {
        assert(d == 0);
    }
    for (size_t i = 0; i < clients_.size(); i++) {
        if (clients_[i].GetTotalAllocation() != demand[i]) {
            printf("%ldth client, total alocation: %d, demand: %d\n", i, clients_[i].GetTotalAllocation(), demand[i]);
        }
        assert(clients_[i].GetTotalAllocation() == demand[i]);
    }
    WriteSchedule();
}


int SystemManager::GetFullTimes(const std::vector<int> &demand) {
    ll cur_demand = std::accumulate(demand.begin(), demand.end(), 0);
    if (cur_demand <= mid_demand_) {
        return 0;
    }
    printf("cur_demand - mid_demand_ = %lld, cur_demand / over_demand_all_ = %.2lf\n", cur_demand, 1.0 * (cur_demand - mid_demand_) / over_demand_all_);
    printf("all full times = %d\n", all_full_times_);
    return static_cast<int>(1.0 * (cur_demand - mid_demand_) / over_demand_all_ * all_full_times_);
}

void SystemManager::GreedyAllocate(std::vector<int> &demand) {
    int cur_full_times = GetFullTimes(demand);
    printf("cur full times = %d\n", cur_full_times);
    if (cur_full_times == 0) {
        return;
    }
    for (int full_times = 0; full_times < cur_full_times; full_times++) {
        int max_site_sum = 0;
        int max_site_idx = -1;
        for (size_t i = 0; i < sites_.size(); i++) {
            if (!sites_[i].IsSafe()) {
                continue;
            }
            int cur_sum = 0;
            for (int cli_idx : sites_[i].GetRefClients()) {
                cur_sum += demand[cli_idx];
            }
            if (std::min(cur_sum, sites_[i].GetRemainBandwidth()) > max_site_sum) {
                max_site_sum = cur_sum;
                max_site_idx = i;
            }
        }
        /* printf("choose server %d\n", max_site_idx); */
        if (max_site_idx == -1) {
            break;
        }
        auto &site = sites_[max_site_idx];
        assert(site.IsSafe());
        for (int c : site.GetRefClients()) {
            if (demand[c] == 0) {
                continue;
            }
            int allocated = std::min(site.GetRemainBandwidth(), demand[c]);
            assert(clients_[c].SetAllocationBySite(max_site_idx, allocated));
            demand[c] -= allocated;
            site.DecreaseBandwith(allocated);
            if (site.GetRemainBandwidth() == 0) {
                break;
            }
        }
        site.IncFullTimes();
    }
}

void SystemManager::AverageAllocate(std::vector<int> &demand) {
    for (size_t i = 0; i < demand.size(); i++) {
        // client node i
        auto &client = clients_[i];
        if (demand[i] == 0) {
            continue;
        }
        int cur_demand;
        while (true) {
            cur_demand = demand[i];
            // 每次循环重设retio避免死循环
            SetClientRatio(i, demand);
            int total = 0;
            int tmp;
            size_t j;
            // for every server j
            for (j = 0; j < client.GetSiteCount() - 1; j++) {
                auto &site = GetSite(i, j);
                if (site.GetRemainBandwidth() == 0) {
                    continue;
                }
                if (cur_demand < 50 &&
                    cur_demand <= site.GetRemainBandwidth()) {
                    site.DecreaseBandwith(cur_demand);
                    demand[i] = 0;
                    client.AddAllocation(j, cur_demand);
                    goto finish;
                }
                tmp = static_cast<int>(client.GetRatio(j) * cur_demand);
                tmp = std::min(tmp, site.GetRemainBandwidth());
                client.AddAllocation(j, tmp);
                total += tmp;
                site.DecreaseBandwith(tmp);
            }
            // the last one should be calculated by substraction
            auto &site = GetSite(i, j);
            tmp = cur_demand - total;
            tmp = std::min(tmp, site.GetRemainBandwidth());
            client.AddAllocation(j, tmp);
            total += tmp;
            site.DecreaseBandwith(tmp);
            demand[i] -= total;
            if (demand[i] == 0) {
                break;
            }
        }
    finish:;
    }
}

void SystemManager::ReadDemands() {
    std::vector<int> demand;
    std::vector<ll> v;
    while (file_parser_.ParseDemand(clients_.size(), demand)) {
        ll demand_sum = std::accumulate(demand.begin(), demand.end(), 0);
        v.push_back(demand_sum);
        total_demand_ += demand_sum;
        demands_.push_back(demand);
        if (demand_sum > max_demand_) {
            max_demand_ = demand_sum;
        }
    }
    std::sort(v.begin(), v.end());
    mid_demand_ = v[v.size() / 2];
    for (size_t i = v.size() / 2 + 1; i < v.size(); i++) {
        over_demand_all_ += (v[i] - mid_demand_);
    }
    avg_demand_ = total_demand_ / demands_.size();
    int site_full_times = static_cast<int>(demands_.size() * 0.05);
    for (auto &site : sites_) {
        site.SetMaxFullTimes(site_full_times - 1);
    }
    all_full_times_ = static_cast<int>(demands_.size() * 0.05 * sites_.size());
    printf("all full times = %d\n", all_full_times_);
    printf("site max full times = %d\n", site_full_times);
    printf("total demand = %lld\n", total_demand_);
    printf("average demand = %lld\n", avg_demand_);
    printf("mid demand = %lld\n", mid_demand_);
    printf("max demand = %lld\n", max_demand_);
    printf("over demand all = %lld\n", over_demand_all_);
}

void SystemManager::WriteSchedule() {
    for (size_t i = 0; i < clients_.size(); i++) {
        fprintf(output_fp_, "%s:", clients_[i].GetName());
        bool flag = false;
        for (size_t j = 0; j < clients_[i].GetSiteCount(); j++) {
            int site_idx = clients_[i].GetSiteIndex(j);
            if (clients_[i].GetSiteAllocation(j) > 0) {
                if (flag) {
                    fprintf(output_fp_, ",");
                }
                fprintf(output_fp_, "<%s,%d>", sites_[site_idx].GetName(),
                        clients_[i].GetSiteAllocation(j));
                flag = true;
            }
        }
        fprintf(output_fp_, "\n");
    }
}

int main() {
    SystemManager manager("/output/solution.txt");
    manager.Init();
    manager.Process();
}
