#include <cassert>
#include <cmath>
#include <iostream>
#include <list>
#include <numeric>
#include <memory>
#include <random>

#include "file_parser.hpp"
#include "set_comparator.hpp"

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
    ll avg_demand_;
    int each_time_full_count_;
    int next_full_idx_{0};
    std::unique_ptr<SetCompartor> set_comparator_{nullptr};

    // 根据服务器的当前容量，以及被访问次数，动态计算流量分配ratio
    void SetClientRatio(int i);
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
    set_comparator_ = MakeUnique<SetCompartor>(sites_);
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
}

void SystemManager::SetClientRatio(int i) {
    auto &client = clients_[i];
    double sum = 0;
    std::vector<double> inter_values(client.GetSiteCount());
    for (size_t j = 0; j < client.GetSiteCount(); j++) {
        const auto &site = GetSite(i, j);
        inter_values[j] =
            (1.0 / site.GetRefTimes() * site.GetRemainBandwidth());
        sum += inter_values[j];
    }
    // 当前client的所有可以访问的节点都没有剩余容量了，寄！
    assert(sum != 0);
    for (size_t j = 0; j < client.GetSiteCount(); j++) {
        client.SetRatio(j, inter_values[j] / sum);
    }
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
        assert(clients_[i].GetTotalAllocation() == demand[i]);
    }
    WriteSchedule();
}

void SystemManager::GreedyAllocate(std::vector<int> &demand) {
    ll cur_demand_all = std::accumulate(demand.begin(), demand.end(), 0);
    int cur_full_times = static_cast<int>(
        round(cur_demand_all / avg_demand_ * each_time_full_count_));
    if (cur_full_times == 0) {
        return;
    }
    int min_ref_times = INT32_MAX;
    int min_ref_idx = -1;
    for (size_t i = 0; i < sites_.size(); i++) {
        if (sites_[i].GetFullTimes() < min_ref_times) {
            min_ref_times = sites_[i].GetRefTimes();
            min_ref_idx = i;
        }
    }
    /* printf("min ref times = %d\n", min_ref_times); */
    /* printf("min ref idx = %d\n", min_ref_idx); */
    auto providers = set_comparator_->IrrelevantSites(min_ref_idx, cur_full_times);
    /* print_vec(providers); */
    for (int i : providers) {
        auto &site = sites_[i];
        assert(site.IsSafe());
        /* if (!site.IsSafe()) { */
        /*     continue; */
        /* } */
        for (int c : site.GetRefClients()) {
            if (demand[c] == 0) {
                continue;
            }
            int allocated = std::min(site.GetRemainBandwidth(), demand[c]);
            assert(clients_[c].SetAllocationBySite(i, allocated));
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
            SetClientRatio(i);
            int total = 0;
            int tmp;
            size_t j;
            // for every server j
            for (j = 0; j < client.GetSiteCount() - 1; j++) {
                auto &site = GetSite(i, j);
                if (site.GetRemainBandwidth() == 0) {
                    continue;
                }
                if (cur_demand < 100 &&
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
    while (file_parser_.ParseDemand(clients_.size(), demand)) {
        total_demand_ += std::accumulate(demand.begin(), demand.end(), 0);
        demands_.push_back(demand);
    }
    avg_demand_ = total_demand_ / demands_.size();
    int full_times = static_cast<int>(demands_.size() * 0.05) - 5;
    for (auto &site : sites_) {
        site.SetMaxFullTimes(full_times);
    }
    each_time_full_count_ = full_times * sites_.size() / demands_.size();
    printf("max full times = %d\n", full_times);
    printf("each time full count = %d\n", each_time_full_count_);
    printf("total demand = %lld\n", total_demand_);
    printf("average demand = %lld\n", avg_demand_);
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
