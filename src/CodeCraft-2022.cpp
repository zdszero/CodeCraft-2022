#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <chrono>
#include <queue>
#include <map>

#include "file_parser.hpp"
#include "daily_site.hpp"

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
    std::vector<std::vector<int>> daily_full_site_idx;
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
    void Schedule(Demand &d, int day);
    // 贪心将可以分配满的site先分配满
    void GreedyAllocate(std::vector<int> &demand, int full_count, int day);
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
    // 获取成绩
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
    for(int i = 0; i < clients_.size(); i++) {
        std::sort(clients_[i].GetAccessibleSite().begin(), clients_[i].GetAccessibleSite().end(),
                  [this](int l, int r) { return sites_[l].GetRefTimes() > sites_[r].GetRefTimes(); });

        for(auto site : clients_[i].GetAccessibleSite()) {
            clients_[i].AddAccessTotal(sites_[site].GetTotalBandwidth());
        }
    }
    ReadDemands();
}

struct tem {
    bool operator() (Daily_site a, Daily_site b) {
        return a.GetTotal() < b.GetTotal();
    }
};

struct Pari_max_95 {
    int Time;
    int value;
};

struct tem_95 {
    bool operator() (Pari_max_95 a, Pari_max_95 b) {
        return a.value < b.value;
    }
};

std::priority_queue<Pari_max_95, std::vector<Pari_max_95>, tem_95> Max_95_site;
std::vector<int> SiteOnlyOne;

void SystemManager::Process() {
    /* std::vector<int> demand; */
    /* while (file_parser_.ParseDemand(clients_.size(), demand)) { */
    /*     Schedule(demand); */
    /* } */
    std::vector<std::priority_queue<Daily_site, std::vector<Daily_site>, tem>> daily_queue;

    int global_full_time = demands_.size() * sites_.size() * 0.05;

    //site index
    std::vector<int> Site_idx;
    std::vector<int> Day_idx;
    for(int i = 0; i < sites_.size(); i++) {
        Site_idx.push_back(i);
    }
    for(int i = 0; i < demands_.size(); i++) {
        Day_idx.push_back(i);
    }
    for (auto &site : sites_) {
        site.SetCurFullTimes(0);
    }


    ll best_grade = std::numeric_limits<ll>::max();
    decltype(results_) best_result;
    std::vector<int> best_site_sep;
    for(int i = 0; i < sites_.size(); i++) {
        best_site_sep.push_back(std::numeric_limits<int>::max());
    }
    // std::sort(demands_.begin(), demands_.end(), [](const Demand &l, const Demand &r) { return l.sum_ > r.sum_; });


    for (size_t i = 0; i < 110; i++) {
        SiteOnlyOne.clear();
        for (auto &site : sites_) {
            site.Restart();
        }
        unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
        std::shuffle(Site_idx.begin(), Site_idx.end(), std::default_random_engine(seed));
        auto demands_copy = demands_;

        daily_full_site_idx.clear();
        for(int demands_idx = 0; demands_idx < demands_.size(); demands_idx++) {
            daily_full_site_idx.push_back(std::vector<int>());
        }
        for(int site_index : Site_idx) {
            std::priority_queue<Daily_site, std::vector<Daily_site>, tem> site_max_req;
            for (int demands_idx = 0; demands_idx < demands_copy.size(); demands_idx++ ) {
                auto &demand = demands_copy[demands_idx].demand_;
                int cur_sum = 0;
                for(int cli_idex : sites_[site_index].GetRefClients()) {
                    cur_sum += demand[cli_idex] * ((sites_[site_index].GetTotalBandwidth() *1.0) / (1.0*clients_[cli_idex].GetAccessTotal()));
                }
                if(cur_sum > 0) {
                    site_max_req.push({demands_idx, site_index, cur_sum,
                                       sites_[site_index].GetTotalBandwidth()});
                }
            }
            for(int j = 0; j < (int)(demands_.size() * 0.05); j++) {
                if(site_max_req.empty()) {
                    break;
                }
                Daily_site daily_site = site_max_req.top();
                int cur_time = daily_site.GetTime();
                int remainBandwidth = daily_site.GetRemainBandwidth();
                auto &demand = demands_copy[cur_time].demand_;
                for (int c : sites_[daily_site.GetSiteIdx()].GetRefClients()) {
                    if (demand[c] == 0) {
                        continue;
                    }
                    int allocated = std::min(remainBandwidth, demand[c]);
                    //assert(clients_[c].SetAllocationBySite(max_site_idx, allocated));
                    demand[c] -= allocated;
                    remainBandwidth -= allocated;
                    if (remainBandwidth == 0) {
                        break;
                    }
                }
                daily_full_site_idx[cur_time].push_back(daily_site.GetSiteIdx());
                site_max_req.pop();
            }
        }

        int day = 0;

//        std::vector<int> day_already;
//        while(!Max_95_site.empty()) {
//            Schedule(demands_[Max_95_site.top().Time], Max_95_site.top().Time);
//            day_already.push_back(Max_95_site.top().Time);
//            Max_95_site.pop();
//        }

        std::shuffle(Day_idx.begin(), Day_idx.end(), std::default_random_engine(seed));
        for(int day_idx : Day_idx){
        //for (auto &d : demands_) {
            /* printf("sum = %lld\n", d.sum_); */
//            if(std::find(day_already.begin(), day_already.end(), day_idx) != day_already.end()) {
//                day++;
//                continue;
//            }
            auto &d = demands_[day_idx];
            Schedule(d, day_idx);
            //day++;

        }
        ll grade = GetGrade();
        printf("grade = %lld\n", grade);
        if (grade < best_grade) {
            best_grade = grade;
            best_result = results_;
        }

//        for(int j = 0; j < sites_.size(); j++) {
//            sites_[j].SetSeperateBandwidth(sites_[j].GetSeperateBandwidth()*1.2 );
//        }
        for(int j = 0; j < sites_.size(); j++) {
            if(sites_[j].GetSeperateBandwidth() < best_site_sep[j]) {
                best_site_sep[i] = sites_[j].GetSeperateBandwidth();
            } else {
                sites_[j].SetSeperateBandwidth(best_site_sep[j]*0.8);
            }
        }

        std::vector<int> max_95_tem(demands_.size());
        for (auto &site : sites_) {
            if(site.GetMax95Time() != -1) {
                max_95_tem[site.GetMax95Time()] += site.GetSeperateBandwidth();
            }
        }
        Max_95_site = std::priority_queue<Pari_max_95, std::vector<Pari_max_95>, tem_95>();
        for(int time_index = 0; time_index < demands_.size(); time_index++) {
            if(max_95_tem[time_index] != 0) {
                Max_95_site.push({time_index, max_95_tem[time_index]});
            }
        }

    }
    printf("best grade = %lld\n", best_grade);
    for (const auto &r : best_result) {
        WriteSchedule(r);
    }
    /* std::sort(demands_.begin(), demands_.end(), [](const Demand &l, const Demand &r) { return l.sum_ > r.sum_; }); */
    /* for (auto &d : demands_) { */
    /*     Schedule(d); */
    /* } */
    /* for (const auto &r : results_) { */
    /*     WriteSchedule(r); */
    /* } */
    // print full times
//    printf("full times: ");
//    for (size_t i = 0; i < sites_.size(); i++) {
//        printf("ref count: %d, full times: %d\n", sites_[i].GetRefTimes(),
//               sites_[i].GetFullTimes());
//    }

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



void SystemManager::Schedule(Demand &d, int day) {
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
//    StableAllocate(demand_cpy);
//    for (size_t i = 0; i < clients_.size(); i++) {
//        assert(clients_[i].GetTotalAllocation() <= d.demand_[i]);
//    }
    // greedy allcoation
//    int cur_req = std::accumulate(demand_cpy.begin(), demand_cpy.end(), 0);
//    int cur_sep = 0;
//    for(size_t i = 0; i < sites_.size(); i++) {
//        cur_sep += (sites_[i].GetSeperateBandwidth()-sites_[i].GetAllocatedBandwidth());
//    }

    GreedyAllocate(demand_cpy, full_times, day);
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
        site.ResetSeperateBandwidth(day);
    }
    /* WriteSchedule(GenerateResult()); */
    results_[d.idx_] = GenerateResult();
}


int SystemManager::GetFullTimes(const std::vector<int> &demand) {
    ll cur_demand = std::accumulate(demand.begin(), demand.end(), 0);
    if (cur_demand <= mid_demand_) {
        return 0;
    }
    return static_cast<int>(1.0 * (cur_demand /*- mid_demand_*/) / over_demand_all_ * all_full_times_);
}



void SystemManager::GreedyAllocate(std::vector<int> &demand, int full_count, int day) {
    /* printf("cur full times = %d\n", full_count); */
    if (full_count == 0) {
        return;
    }

    if(daily_full_site_idx[day].empty()) {
        return ;
    }

    for (int max_site_idx : daily_full_site_idx[day]) {
    //for(int full_times = 0; full_times < full_count+5; full_times++) {
        int max_site_sum = 0;
        /*int max_site_idx = -1;
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
        }*/
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
            assert(clients_[c].AddAllocationBySite(max_site_idx, allocated));
            demand[c] -= allocated;
            site.DecreaseBandwith(allocated);
            if (site.GetRemainBandwidth() == 0) {
                break;
            }
        }
        site.IncFullTimes();
        site.SetFullThisTime();
    }

//    for(int full_times = 0; full_times < 1; full_times++) {
//        int max_site_sum = 0;
//        int max_site_idx = -1;
//        for (size_t i = 0; i < sites_.size(); i++) {
//            if (!sites_[i].IsSafe()) {
//                continue;
//            }
//            int cur_sum = 0;
//            for (int cli_idx : sites_[i].GetRefClients()) {
//                cur_sum += demand[cli_idx];
//            }
//            if (std::min(cur_sum, sites_[i].GetRemainBandwidth()) > max_site_sum &&
//                (std::find(SiteOnlyOne.begin(), SiteOnlyOne.end(), i) == SiteOnlyOne.end())) {
//                max_site_sum = cur_sum;
//                max_site_idx = i;
//            }
//        }
//        /* printf("choose server %d\n", max_site_idx); */
//        if (max_site_idx == -1) {
//            break;
//        }
//        auto &site = sites_[max_site_idx];
//        assert(site.IsSafe());
//        for (int c : site.GetRefClients()) {
//            if (demand[c] == 0) {
//                continue;
//            }
//            int allocated = std::min(site.GetRemainBandwidth(), demand[c]);
//            assert(clients_[c].AddAllocationBySite(max_site_idx, allocated));
//            demand[c] -= allocated;
//            site.DecreaseBandwith(allocated);
//            if (site.GetRemainBandwidth() == 0) {
//                break;
//            }
//        }
//        site.IncFullTimes();
//        site.SetFullThisTime();
//        SiteOnlyOne.push_back(max_site_idx);
//    }




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
    for (size_t C = 0; C < demand.size(); C++) {
        if (demand[C] == 0) {
            continue;
        }
        for (size_t S = 0; S < clients_[C].GetSiteCount(); S++) {
            auto &site = GetSite(C, S);
            if (site.IsFullThisTime()) {
                continue;
            }
            int sep = site.GetSeperateBandwidth()/0.6;
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
    mid_demand_ = v[v.size() / 4];
    for (size_t i = v.size() / 4 + 1; i < v.size(); i++) {
        //over_demand_all_ += (v[i] - mid_demand_);
        over_demand_all_ += v[i];
    }
    avg_demand_ = total_demand_ / demands_.size();
    // 设置每个服务器最多打满的次数
    int site_full_times = static_cast<int>(demands_.size() * 0.05);
    for (auto &site : sites_) {
        site.SetMaxFullTimes(site_full_times);
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
    size_t sep_idx = static_cast<size_t> (0.95 * demands_.size()) -1;
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