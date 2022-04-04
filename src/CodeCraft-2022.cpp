#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <queue>
#include <random>

#include "daily_site.hpp"
#include "file_parser.hpp"
#include "result_set.hpp"

using namespace std;

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
    vector<vector<int>> client_demands_;
    long total_demand_{0};
    long avg_demand_{0};
    long max_demand_{0};
    long mid_demand_{0};
    long over_demand_all_{0};
    int all_full_times_;
    unique_ptr<ResultSet> results_;
    vector<vector<int>> daily_full_site_indexes_;
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
    void WriteSchedule(const Result &res);
    // 根据函数计算当前应该打满次数
    int GetFullTimes(const Demand &d);
    // 获取成绩
    long GetGrade();
    // 预先设定好每天需要打满的服务器
    void PresetMaxSites();
};

void SystemManager::Init() {
    file_parser_.ParseSites(sites_);
    file_parser_.ParseConfig(qos_constraint_, base_cost_);
    file_parser_.ParseQOS(clients_, qos_constraint_);
    std::sort(clients_.begin(), clients_.end(),
              [](const Client &l, const Client &r) {
                  return l.GetSiteCount() < r.GetSiteCount();
              });
    for (size_t cli_idx = 0; cli_idx < clients_.size(); cli_idx++) {
        clients_[cli_idx].SetID(cli_idx);
    }
    file_parser_.RebuildClientMap(clients_);
    while (file_parser_.ParseDemand(clients_.size(), demands_))
        ;
    results_ =
        unique_ptr<ResultSet>(new ResultSet(sites_, clients_, base_cost_));
    /* results_->Reserve(demands_.size()); */
    results_->Resize(demands_.size());
    // 计算每个site被多少client使用
    for (size_t i = 0; i < clients_.size(); i++) {
        for (const auto site_idx : clients_[i].GetAccessibleSite()) {
            sites_[site_idx].AddRefClient(i);
        }
    }
    std::for_each(sites_.begin(), sites_.end(), [this](Site &site) {
        sort(site.GetRefClients().begin(), site.GetRefClients().end(),
             [this](int l, int r) {
                 auto GetAvailable = [this](int cli_idx) -> int {
                     int ret = 0;
                     for (size_t site_idx :
                          clients_[cli_idx].GetAccessibleSite()) {
                         ret += sites_[site_idx].GetTotalBandwidth() /
                                sites_[site_idx].GetRefTimes();
                     }
                     return ret;
                 };
                 return GetAvailable(l) < GetAvailable(r);
             });
    });
    // 将client中的accessible sites根据引用次数排序，并且计算client的总需求量
    std::for_each(clients_.begin(), clients_.end(), [this](Client &cli) {
        sort(cli.GetAccessibleSite().begin(), cli.GetAccessibleSite().end(),
             [this](int l, int r) {
                 return sites_[l].GetRefTimes() < sites_[r].GetRefTimes();
             });

        for (auto site : cli.GetAccessibleSite()) {
            cli.AddAccessTotal(sites_[site].GetTotalBandwidth());
        }
    });
    for (size_t cli_idx = 0; cli_idx < clients_.size(); cli_idx++) {
        clients_[cli_idx].ReInit();
    }
    // set variables
    for (const auto &d : demands_) {
        long cur_total = d.GetTotalDemand();
        if (cur_total > max_demand_) {
            max_demand_ = cur_total;
        }
        total_demand_ += cur_total;
    }
    avg_demand_ = total_demand_ / demands_.size();
    // all_full_times_ = static_cast<int>(demands_.size() * 0.05 *
    // sites_.size());
    for (auto &site : sites_) {
        site.SetMaxFullTimes(demands_.size() / 20 - 1);
    }

    for (size_t i = 0; i < demands_.size(); i++) {
        auto d = demands_[i];
        client_demands_.push_back(vector<int>(clients_.size(), 0));
        for (size_t j = 0; j < clients_.size(); j++) {
            client_demands_[i][j] = d.GetClientDemand(j);
        }
    }
}

struct DailySiteCmp {
    bool operator()(const daily_site &a, const daily_site &b) {
        return a.GetTotal() < b.GetTotal();
    }
};

void SystemManager::PresetMaxSites() {
    std::vector<size_t> max_site_indexes;
    for (size_t i = 0; i < sites_.size(); i++) {
        max_site_indexes.push_back(i);
        sites_[i].SetSeperateBandwidth(base_cost_);
    }
    // Sort by site capacity
    auto sites_copy = sites_;
    sort(max_site_indexes.begin(), max_site_indexes.end(),
         [&sites_copy](size_t l, size_t r) {
             return sites_copy[l].GetTotalBandwidth() >
                    sites_copy[r].GetTotalBandwidth();
         });
    sort(sites_copy.begin(), sites_copy.end(),
         [](const Site &l, const Site &r) {
             return l.GetTotalBandwidth() > r.GetTotalBandwidth();
         });

    // 按照site容量降序计算在哪一天打满
    auto client_demands_cpy = client_demands_;
    daily_full_site_indexes_.resize(demands_.size(), vector<int>());
    for (size_t site_idx : max_site_indexes) {
        std::priority_queue<daily_site, std::vector<daily_site>, DailySiteCmp>
            site_max_req;
        // 找出每个服务器打得最满的%5天
        for (size_t demands_idx = 0; demands_idx < client_demands_cpy.size();
             demands_idx++) {
            int cur_sum = 0;
            for (size_t cli_idx : sites_[site_idx].GetRefClients()) {
                cur_sum += client_demands_cpy[demands_idx][cli_idx] /
                           clients_[cli_idx].GetSiteCount();
                //((sites_[site_index].GetTotalBandwidth() * 1.0) /
                //(1.0 * clients_[cli_idex].GetAccessTotal()));
            }
            if (cur_sum > 0) {
                site_max_req.push(
                    {demands_idx, site_idx,
                     cur_sum - sites_[site_idx].GetSeperateBandwidth(),
                     sites_[site_idx].GetTotalBandwidth()});
            }
        }
        for (int j = 0; j < (int)(demands_.size() * 0.05); j++) {
            if (site_max_req.empty()) {
                break;
            }
            daily_site daily_site = site_max_req.top();
            int cur_time = daily_site.GetTime();
            int remainBandwidth = daily_site.GetRemainBandwidth();
            auto &demand = client_demands_cpy[cur_time];
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
            daily_full_site_indexes_[cur_time].push_back(
                daily_site.GetSiteIdx());
            site_max_req.pop();
        }
    }
}

void SystemManager::Process() {
    PresetMaxSites();
    // 对访问demand的顺序进行排序
    vector<size_t> days;
    for (size_t day_idx = 0; day_idx < demands_.size(); day_idx++) {
        days.push_back(day_idx);
    }
    sort(days.begin(), days.end(), [this](size_t l, size_t r) {
        return demands_[l].GetTotalDemand() < demands_[r].GetTotalDemand();
    });
    for (size_t day_idx : days) {
        /* for (size_t day_idx = 0; day_idx < demands_.size(); day_idx++) { */
        auto &d = demands_[day_idx];
        Schedule(d, day_idx);
    }
    for (size_t times = 0; times < 5; times++) {
        results_->Migrate();
    }
    printf("grade = %d\n", results_->GetGrade());
    for (const auto &day_res : *results_) {
        WriteSchedule(day_res);
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
    /* results_->AddResult(Result(clients_, sites_)); */
    results_->SetResult(day, Result(clients_, sites_));
}

void SystemManager::GreedyAllocate(Demand &d, int day) {
    auto &need = d.GetStreamDemands();
    if (daily_full_site_indexes_[day].empty()) {
        return;
    }
    for (int max_site_idx : daily_full_site_indexes_[day]) {
        if (max_site_idx == -1) {
            return;
        }
        auto &site = sites_[max_site_idx];
        for (size_t cli_idx : site.GetRefClients()) {
            for (auto it = need.begin(); it != need.end(); it++) {
                if (it->second[cli_idx] > site.GetRemainBandwidth()) {
                    continue;
                }
                if (it->second[cli_idx] == 0) {
                    continue;
                }
                /* site.DecreaseBandwidth(it->second[C]); */
                site.AddStream(Stream{cli_idx,
                                      static_cast<size_t>(max_site_idx),
                                      it->first, it->second[cli_idx]});
                clients_[cli_idx].AddStreamBySiteIndex(
                    max_site_idx,
                    Stream{cli_idx, static_cast<size_t>(max_site_idx),
                           it->first, it->second[cli_idx]});
                it->second[cli_idx] = 0;
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
        for (size_t cli_idx = 0; cli_idx < v.size(); cli_idx++) {
            if (v[cli_idx] == 0) {
                continue;
            }
            int sep = 0;
            int allocated = 0;
            auto &cli = clients_[cli_idx];
            auto &site_indexes = cli.GetAccessibleSite();
            for (size_t S = 0; S < site_indexes.size(); S++) {
                size_t site_idx = site_indexes[S];
                auto &site = sites_[site_idx];
                if (site.IsFullThisTime()) {
                    continue;
                }
                sep = site.GetSeperateBandwidth();
                allocated = site.GetAllocatedBandwidth();
                if (allocated >= sep - v[cli_idx]) {
                    continue;
                }
                if (site.GetRemainBandwidth() >= v[cli_idx]) {
                    /* site.DecreaseBandwidth(v[C]); */
                    site.AddStream(
                        Stream{cli_idx, site_idx, stream_name, v[cli_idx]});
                    cli.AddStream(
                        S, Stream{cli_idx, site_idx, stream_name, v[cli_idx]});
                    v[cli_idx] = 0;
                    break;
                }
            }
            // assert(flag == true);
        }
    }
}

void SystemManager::AverageAllocate(Demand &d) {
    auto &need = d.GetStreamDemands();
    for (auto it = need.begin(); it != need.end(); it++) {
        string stream_name = it->first;
        auto &v = it->second;
        // client idx i
        for (size_t cli_idx = 0; cli_idx < v.size(); cli_idx++) {
            if (v[cli_idx] == 0) {
                continue;
            }
            auto &cli = clients_[cli_idx];
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
                if (site.GetRemainBandwidth() >= v[cli_idx]) {
                    used = site.GetAllocatedBandwidth() + v[cli_idx];
                    sep = site.GetSeperateBandwidth();
                    if (used <= sep) {
                        min_site = site_idx;
                        min_S = S;
                        min_value = grade;
                        flag = true;
                        break;
                    }
                    grade = ((used * used - sep * sep -
                              2 * base_cost_ * (used - sep)) *
                             1.0) /
                                (site.GetTotalBandwidth() * 1.0) +
                            (used - sep);
                    if (grade < min_value) {
                        min_site = site_idx;
                        min_S = S;
                        min_value = grade;
                        flag = true;
                    }
                }
            }
            auto &site = sites_[min_site];
            /* site.DecreaseBandwidth(v[C]); */
            site.AddStream(Stream{cli_idx, static_cast<size_t>(min_site),
                                  stream_name, v[cli_idx]});
            site.ResetSeperateBandwidth();
            cli.AddStream(min_S, Stream{cli_idx, static_cast<size_t>(min_site),
                                        stream_name, v[cli_idx]});
            v[cli_idx] = 0;
            assert(flag == true);
        }
    }
}

void SystemManager::WriteSchedule(const Result &res) {
    // for each client index i
    for (size_t cli_idx = 0; cli_idx < clients_.size(); cli_idx++) {
        fprintf(output_fp_, "%s:", clients_[cli_idx].GetName());
        bool flag = false;
        // for each accessible server j
        for (size_t S = 0; S < res.GetClientAccessibleSiteCount(cli_idx); S++) {
            const auto &allocate_list = res.GetAllocationTable(cli_idx, S);
            int site_idx = clients_[cli_idx].GetSiteIndex(S);
            if (!allocate_list.empty()) {
                if (flag) {
                    fprintf(output_fp_, ",");
                }
                fprintf(output_fp_, "<%s", sites_[site_idx].GetName());
                for (auto &stream : allocate_list) {
                    fprintf(output_fp_, ",%s", stream.stream_name.c_str());
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
