#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>

#include "file_parser.hpp"

#define ll long long
using single_result_t = std::vector<std::vector<int>>;

template <typename T> void print_vec(std::vector<T> &v) {
    for (T val : v) {
        std::cout << val << " ";
    }
    std::cout << std::endl;
}

struct Demand {
    size_t idx_;
    std::vector<int> demand_;
    ll sum_;
    Demand() = default;
    Demand(size_t idx, std::vector<int> &&demand): idx_(idx), demand_(std::move(demand)) {
        sum_ = std::accumulate(demand_.begin(), demand_.end(), 0LL);
    }
};

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
    std::vector<Demand> demands_;
    ll total_demand_{0};
    ll avg_demand_{0};
    ll max_demand_{0};
    ll mid_demand_{0};
    ll over_demand_all_{0};
    int all_full_times_;
    std::vector<single_result_t> results_;

    // 根据服务器的当前容量，以及被访问次数，动态计算流量分配ratio
    void SetClientRatio(int i, const std::vector<int> &demand);
    // 对于每一个时间戳的请求进行调度
    void Schedule(Demand &d);
    // 贪心将可以分配满的site先分配满
    void GreedyAllocate(std::vector<int> &demand, int full_count);
    // 尽量使得每个site的分配量保持稳定
    void StableAllocate(std::vector<int> &demand);
    // 平均分配
    void AverageAllocate(std::vector<int> &demand);
    // 获取第i个client的第j个边缘结点
    Site &GetSite(int i, int j) { return sites_[clients_[i].GetSiteIndex(j)]; }
    // 获取所有的请求
    void ReadDemands();
    // 向/output/solution.txt中写出结果
    void WriteSchedule(single_result_t res);
    // 计算site当前被引用的需求总和
    ll GetSiteTotalDemand(const Site &site, const std::vector<int> &demand);
    // 根据函数计算当前应该打满次数
    int GetFullTimes(const std::vector<int> &demand);
    // 从clients中提取allocation table
    single_result_t GenerateResult();
    ll GetGrade();
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
    /* std::sort(demands_.begin(), demands_.end(), [](const Demand &l, const Demand &r) { return l.sum_ > r.sum_; }); */
    ll best_grade = std::numeric_limits<ll>::max();
    decltype(results_) best_result;
    for (size_t i = 0; i < 100; i++) {
        for (auto &site : sites_) {
            site.Restart();
        }
        for (auto &d : demands_) {
            /* printf("sum = %lld\n", d.sum_); */
            Schedule(d);
        }
        ll grade = GetGrade();
        printf("grade = %lld\n", grade);
        if (grade < best_grade) {
            best_grade = grade;
            best_result = results_;
        }
        std::shuffle(demands_.begin(), demands_.end(), std::default_random_engine{});
    }
    printf("best grade = %lld\n", best_grade);
    for (const auto &r : best_result) {
        WriteSchedule(r);
    }
    /* for (auto &d : demands_) { */
    /*     Schedule(d); */
    /* } */
    /* for (const auto &r : results_) { */
    /*     WriteSchedule(r); */
    /* } */
    // print full times
    /* printf("full times: "); */
    /* for (size_t i = 0; i < sites_.size(); i++) { */
    /*     printf("ref count: %d, full times: %d\n", sites_[i].GetRefTimes(), */
    /*            sites_[i].GetFullTimes()); */
    /* } */
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

void SystemManager::Schedule(Demand &d) {
    // 重设所有server的剩余流量
    for (auto &site : sites_) {
        site.Reset();
    }
    for (auto &client : clients_) {
        client.Reset();
    }
    auto demand_cpy = d.demand_;
    int full_times = GetFullTimes(demand_cpy);
    // stable allcoation
    StableAllocate(demand_cpy);
    for (size_t i = 0; i < clients_.size(); i++) {
        assert(clients_[i].GetTotalAllocation() <= d.demand_[i]);
    }
    // greedy allcoation
    GreedyAllocate(demand_cpy, full_times);
    for (size_t i = 0; i < clients_.size(); i++) {
        assert(clients_[i].GetTotalAllocation() <= d.demand_[i]);
    }
    // stable allcoation
    StableAllocate(demand_cpy);
    for (size_t i = 0; i < clients_.size(); i++) {
        assert(clients_[i].GetTotalAllocation() <= d.demand_[i]);
    }
    // average allocation
    AverageAllocate(demand_cpy);
    for (int d : demand_cpy) {
        assert(d == 0);
    }
    for (size_t i = 0; i < clients_.size(); i++) {
        if (clients_[i].GetTotalAllocation() != d.demand_[i]) {
            printf("%ldth client, total alocation: %d, demand: %d\n", i, clients_[i].GetTotalAllocation(), d.demand_[i]);
        }
        assert(clients_[i].GetTotalAllocation() == d.demand_[i]);
    }
    // update sites seperate value
    for (auto &site : sites_) {
        site.ResetSeperateBandwidth();
    }
    /* WriteSchedule(GenerateResult()); */
    results_[d.idx_] = GenerateResult();
}


int SystemManager::GetFullTimes(const std::vector<int> &demand) {
    ll cur_demand = std::accumulate(demand.begin(), demand.end(), 0);
    if (cur_demand <= mid_demand_) {
        return 0;
    }
    return static_cast<int>(1.0 * (cur_demand - mid_demand_) / over_demand_all_ * all_full_times_);
}

void SystemManager::GreedyAllocate(std::vector<int> &demand, int full_count) {
    if (full_count == 0) {
        return;
    }
    /* printf("cur full times = %d\n", full_count); */
    int increased_times = 0;
    for (int full_times = 0; full_times < full_count; full_times++) {
        int max_site_sum = std::numeric_limits<int>::min();
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
        /* printf("choose max site: %d with sum %d\n", max_site_idx, max_site_sum); */
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
            assert(clients_[c].AddAllocationBySite(max_site_idx, allocated));
            demand[c] -= allocated;
            site.DecreaseBandwith(allocated);
            if (site.GetRemainBandwidth() == 0) {
                break;
            }
        }
        site.IncFullTimes();
        site.SetFullThisTime();
        increased_times++;
    }
    /* printf("increased times = %d\n", increased_times); */
    /* if (increased_times < full_count) { */
    /*     for (int d : demand) { */
    /*         assert(d == 0); */
    /*     } */
    /* } */
}

void SystemManager::StableAllocate(std::vector<int> &demand) {
    for (size_t C = 0; C < demand.size(); C++) {
        if (demand[C] == 0) {
            continue;
        }
        for (size_t S = 0; S < clients_[C].GetSiteCount(); S++) {
            auto &site = GetSite(C, S);
            if (site.IsFullThisTime()) {
                continue;
            }
            int sep = site.GetSeperateBandwidth();
            if (sep == 0) {
                continue;
            }
            int allocated = site.GetAllocatedBandwidth();
            if (allocated >= sep) {
                continue;
            }
            int inc_allocated = std::min(sep - allocated, demand[C]);
            site.DecreaseBandwith(inc_allocated);
            demand[C] -= inc_allocated;
            clients_[C].AddAllocation(S, inc_allocated);
            if (demand[C] == 0) {
                break;
            }
        }
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
    size_t cur_idx = 0;
    while (file_parser_.ParseDemand(clients_.size(), demand)) {
        Demand tmpd = Demand(cur_idx++, std::move(demand));
        v.push_back(tmpd.sum_);
        total_demand_ += tmpd.sum_;
        if (tmpd.sum_ > max_demand_) {
            max_demand_ = tmpd.sum_;
        }
        demands_.push_back(std::move(tmpd));
    }
    // 根据demands_设置结果集的大小
    results_.resize(demands_.size());
    // 只是统计数据，不在这里对demands_根据sum_排序
    std::sort(v.begin(), v.end());
    mid_demand_ = v[v.size() / 2];
    for (size_t i = v.size() / 2 + 1; i < v.size(); i++) {
        over_demand_all_ += (v[i] - mid_demand_);
    }
    avg_demand_ = total_demand_ / demands_.size();
    // 设置每个服务器最多打满的次数
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

single_result_t SystemManager::GenerateResult() {
    single_result_t v;
    for (const auto &c : clients_) {
        v.push_back(c.GetAllocationTable());
    }
    return v;
}

ll SystemManager::GetGrade() {
    std::vector<std::vector<int>> a(sites_.size(), std::vector<int>(demands_.size(), 0));
    // the kth timestamp
    for (size_t k = 0; k < results_.size(); k++) {
        const auto &res = results_[k];
        // for client i
        for (size_t i = 0; i < res.size(); i++) {
            // for each server site_idx
            for (size_t j = 0; j < res[i].size(); j++) {
                int site_idx = clients_[i].GetSiteIndex(j);
                a[site_idx][k] += res[i][j];
            }
        }
    }
    ll grade = 0LL;
    size_t sep_idx = static_cast<size_t> (0.95 * demands_.size()) - 1;
    for (auto &v : a) {
        std::sort(v.begin(), v.end());
        grade += v[sep_idx];
    }
    return grade;
}

void SystemManager::WriteSchedule(single_result_t res) {
    for (size_t i = 0; i < res.size(); i++) {
        fprintf(output_fp_, "%s:", clients_[i].GetName());
        bool flag = false;
        for (size_t j = 0; j < res[i].size(); j++) {
            int site_idx = clients_[i].GetSiteIndex(j);
            if (res[i][j] > 0) {
                if (flag) {
                    fprintf(output_fp_, ",");
                }
                fprintf(output_fp_, "<%s,%d>", sites_[site_idx].GetName(),
                        res[i][j]);
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
