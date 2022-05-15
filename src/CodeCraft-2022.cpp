#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <queue>
#include <random>

#include "center_result_set.hpp"
#include "daily_site.hpp"
#include "file_parser.hpp"
#include "result_set.hpp"

using namespace std;
using DemandIter = unordered_map<string, vector<int>>::iterator;

class SystemManager {
  public:
    SystemManager() = default;
    SystemManager(const string &output_filename) { output_fp_ = fopen(output_filename.c_str(), "w"); }
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
    double center_cost_;
    vector<Site> sites_;
    vector<Client> clients_;
    vector<Demand> demands_; // demands all mtimes
    vector<vector<int>> client_demands_;
    unique_ptr<ResultSet> results_;
    CenterResultSet center_results_;
    vector<vector<size_t>> daily_full_site_indexes_;

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
    file_parser_.ParseConfig(qos_constraint_, base_cost_, center_cost_);
    file_parser_.ParseQOS(clients_, qos_constraint_);
    // 向服务器中添加客户
    for (size_t i = 0; i < clients_.size(); i++) {
        for (const auto site_idx : clients_[i].GetAccessibleSite()) {
            sites_[site_idx].AddRefClient(i);
        }
    }
    // 对于每一个服务器
    std::for_each(sites_.begin(), sites_.end(), [this](Site &site) {
        // 对服务器中的客户进行排序
        sort(site.GetRefClients().begin(), site.GetRefClients().end(), [this](int l, int r) {
            auto GetAvailable = [this](int cli_idx) -> int {
                int ret = 0;
                for (size_t site_idx : clients_[cli_idx].GetAccessibleSite()) {
                    ret += sites_[site_idx].GetTotalBandwidth() / sites_[site_idx].GetRefTimes();
                }
                return ret;
            };
            return GetAvailable(l) < GetAvailable(r);
        });
    });
    // 对客户进行排序
    std::sort(clients_.begin(), clients_.end(),
              [](const Client &l, const Client &r) { return l.GetSiteCount() < r.GetSiteCount(); });
    // std::sort(clients_.begin(), clients_.end(),
    //           [this](const Client &l, const Client &r) {
    //               auto GetAvailable = [this](const Client &cli) -> int {
    //                   int ret = 0;
    //                   for (size_t site_idx : cli.GetAccessibleSite()) {
    //                       ret += sites_[site_idx].GetTotalBandwidth() /
    //                              sites_[site_idx].GetRefTimes();
    //                   }
    //                   return ret;
    //               };
    //               return GetAvailable(l) < GetAvailable(r);
    //           });
    unordered_map<size_t, size_t> cli_idx_map;
    for (size_t cli_idx = 0; cli_idx < clients_.size(); cli_idx++) {
        cli_idx_map[clients_[cli_idx].GetID()] = cli_idx;
        clients_[cli_idx].SetID(cli_idx);
    }
    // 对于每一个客户
    std::for_each(clients_.begin(), clients_.end(), [this](Client &cli) {
        // 计算client的可以被提供的量
        for (auto site_idx : cli.GetAccessibleSite()) {
            auto &site = sites_[site_idx];
            cli.AddAccessTotal(site.GetTotalBandwidth() / site.GetRefTimes());
        }
    });
    std::for_each(clients_.begin(), clients_.end(), [this](Client &cli) {
        // 将客户中的服务器进行排序
        sort(cli.GetAccessibleSite().begin(), cli.GetAccessibleSite().end(),
             [this](int l, int r) { return sites_[l].GetRefTimes() < sites_[r].GetRefTimes(); });
        // sort(cli.GetAccessibleSite().begin(), cli.GetAccessibleSite().end(),
        //      [this](int l, int r) {
        //          auto RefClientsNeed = [this](int site_idx) -> long {
        //             long ret = 0;
        //             auto &site = sites_[site_idx];
        //             for (size_t cli_idx : site.GetRefClients()) {
        //                 ret += clients_[cli_idx].GetAccessTotal();
        //             }
        //             return ret;
        //          };
        //          return RefClientsNeed(l) > RefClientsNeed(r);
        //      });
    });
    // 排序后需要改变原来服务器和file_parser中对应的下标
    for (size_t cli_idx = 0; cli_idx < clients_.size(); cli_idx++) {
        clients_[cli_idx].ReInit();
    }
    file_parser_.RebuildClientMap(clients_);
    for (auto &site : sites_) {
        site.ResetClientIndex(cli_idx_map);
    }
    // 读取所有时刻的请求
    while (file_parser_.ParseDemand(clients_.size(), demands_))
        ;
    results_ = unique_ptr<ResultSet>(new ResultSet(sites_, clients_, base_cost_));
    // results_->Reserve(demands_.size());
    results_->Resize(demands_.size());
    center_results_.Resize(demands_.size());

    // 根据所有时刻的请求初始化一些信息
    for (auto &site : sites_) {
        site.SetMaxFullTimes(demands_.size() / 20);
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
    bool operator()(const DailySite &a, const DailySite &b) { return a.GetTotal() < b.GetTotal(); }
};

void SystemManager::PresetMaxSites() {
    std::vector<size_t> max_site_indexes;
    for (size_t i = 0; i < sites_.size(); i++) {
        max_site_indexes.push_back(i);
        sites_[i].SetSeperateBandwidth(base_cost_);
    }
    // Sort by site capacity
    auto sites_copy = sites_;
    sort(max_site_indexes.begin(), max_site_indexes.end(), [&sites_copy](size_t l, size_t r) {
        // return sites_copy[l].GetTotalBandwidth() > sites_copy[r].GetTotalBandwidth();
        int ltimes = sites_copy[l].GetRefTimes();
        int rtimes = sites_copy[r].GetRefTimes();
        if (ltimes != rtimes) {
            return ltimes > rtimes;
        }
        return sites_copy[l].GetTotalBandwidth() > sites_copy[r].GetTotalBandwidth();
    });
    sort(sites_copy.begin(), sites_copy.end(), [](const Site &l, const Site &r) {
        // return l.GetTotalBandwidth() > r.GetTotalBandwidth();
        int ltimes = l.GetRefTimes();
        int rtimes = r.GetRefTimes();
        if (ltimes != rtimes) {
            return ltimes > rtimes;
        }
        return l.GetTotalBandwidth() > r.GetTotalBandwidth();
    });

    auto client_demands_cpy = client_demands_;
    auto demand_copy = demands_;
    daily_full_site_indexes_.resize(demands_.size(), vector<size_t>());
    for (size_t site_idx : max_site_indexes) {
        auto site = sites_[site_idx];
        std::priority_queue<DailySite, std::vector<DailySite>, DailySiteCmp> site_max_req;
        for (size_t day = 0; day < client_demands_cpy.size(); day++) {

            site.Reset();
            int cur_sum = 0;
            auto &need = demand_copy[day].GetStreamDemands();
            for (auto it = need.begin(); it != need.end(); it++) {
                for (size_t cli_idx : sites_[site_idx].GetRefClients()) {
                    int str_size = it->second[cli_idx];
                    if (str_size == 0)
                        continue;
                    if (str_size / clients_[cli_idx].GetSiteCount() > site.GetRemainBandwidth()) {
                        continue;
                    }
                    cur_sum += str_size / clients_[cli_idx].GetSiteCount();
                    site.DecreaseBandwidth(str_size / clients_[cli_idx].GetSiteCount());
                }
            }
            if (cur_sum > 0) {
                site_max_req.push({day, site_idx, cur_sum, sites_[site_idx].GetTotalBandwidth()});
            }
        }
        for (int j = 0; j < (int)(demands_.size() * 0.05); j++) {
            if (site_max_req.empty()) {
                break;
            }

            DailySite daily_site = site_max_req.top();
            int day = daily_site.GetTime();
            auto site = sites_[daily_site.GetSiteIdx()];
            site.Reset();
            auto &need = demand_copy[day].GetStreamDemands();

            vector<pair<DemandIter, int>> sums;
            sums.reserve(need.size());
            for (auto it = need.begin(); it != need.end(); it++) {
                int sumv = 0;
                for (size_t cli_idx : site.GetRefClients()) {
                    sumv += it->second[cli_idx];
                }
                sums.push_back({it, sumv});
            }
            std::sort(sums.begin(), sums.end(), [](const pair<DemandIter, int> &l, const pair<DemandIter, int> &r) {
                return l.second > r.second;
            });

            vector<pair<size_t, int>> cli_strs;
            cli_strs.reserve(site.GetRefTimes());
            for (const auto &p : sums) {
                auto it = p.first;
                cli_strs.clear();
                for (size_t cli_idx : site.GetRefClients()) {
                    cli_strs.push_back({cli_idx, it->second[cli_idx]});
                }
                std::sort(cli_strs.begin(), cli_strs.end(),
                          [](const pair<size_t, int> &l, const pair<size_t, int> &r) { return l.second < r.second; });
                int i;
                for (i = cli_strs.size() - 1; i >= 0; i--) {
                    size_t cli_idx = cli_strs[i].first;
                    int str_size = cli_strs[i].second;
                    if (str_size > site.GetRemainBandwidth()) {
                        break;
                    }
                    if (str_size == 0) {
                        goto next_round;
                    }
                    client_demands_cpy[day][cli_idx] = 0;
                    it->second[cli_idx] = 0;
                    site.DecreaseBandwidth(str_size);
                }
                if (i >= 0) {
                    for (int j = 0; j < i; j++) {
                        size_t cli_idx = cli_strs[j].first;
                        int str_size = cli_strs[j].second;
                        if (str_size > site.GetRemainBandwidth()) {
                            goto site_full;
                        }
                        if (str_size == 0) {
                            continue;
                        }
                        client_demands_cpy[day][cli_idx] = 0;
                        it->second[cli_idx] = 0;
                        site.DecreaseBandwidth(str_size);
                    }
                }
            next_round:;
            }
        site_full:;

            daily_full_site_indexes_[day].push_back(daily_site.GetSiteIdx());
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
    for (size_t day_idx : days) {
        // for (size_t day_idx = 0; day_idx < demands_.size(); day_idx++) {
        auto &d = demands_[day_idx];
        Schedule(d, day_idx);
    }

    // for (size_t times = 1; times <= 100; times++) {
    //     results_->Migrate();
    //     if (times % 10 == 0) {
    //         results_->AdjustTop5();
    //     }
    // }

    int grade = results_->GetGrade();
    printf("grade = %d\n", grade);
    int center_grade = center_results_.GetGrade();
    printf("center grade = %d\n", center_grade);
    int total_grade = grade + center_grade * center_cost_;
    printf("total grade = %d\n", total_grade);
    // center_results_.PrintGrade();
    for (const auto &day_res : *results_) {
        WriteSchedule(day_res);
    }
    // results_->UpdateTop5();
    // results_->ExpelTop5();
    // for (auto &site : sites_) {
    //     site.PrintClients();
    // }
    // for (auto &cli : clients_) {
    //     cli.PrintSites();
    // }
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
    // results_->AddResult(Result(clients_, sites_));
    results_->SetResult(day, Result(day, clients_, sites_));
    center_results_.SetResult(day, sites_);
}

void SystemManager::GreedyAllocate(Demand &d, int day) {
    auto &need = d.GetStreamDemands();
    if (daily_full_site_indexes_[day].empty()) {
        return;
    }
    for (size_t max_site_idx : daily_full_site_indexes_[day]) {
        if (max_site_idx == -1) {
            return;
        }
        auto &site = sites_[max_site_idx];

        vector<pair<DemandIter, int>> sums;
        sums.reserve(need.size());
        for (auto it = need.begin(); it != need.end(); it++) {
            int sumv = 0;
            for (size_t cli_idx : site.GetRefClients()) {
                sumv += it->second[cli_idx];
            }
            sums.push_back({it, sumv});
        }
        std::sort(sums.begin(), sums.end(),
                  [](const pair<DemandIter, int> &l, const pair<DemandIter, int> &r) { return l.second > r.second; });

        vector<pair<size_t, int>> cli_strs;
        cli_strs.reserve(site.GetRefTimes());
        for (const auto &p : sums) {
            auto it = p.first;
            cli_strs.clear();
            for (size_t cli_idx : site.GetRefClients()) {
                cli_strs.push_back({cli_idx, it->second[cli_idx]});
            }
            std::sort(cli_strs.begin(), cli_strs.end(),
                      [](const pair<size_t, int> &l, const pair<size_t, int> &r) { return l.second < r.second; });
            int i;
            for (i = cli_strs.size() - 1; i >= 0; i--) {
                size_t cli_idx = cli_strs[i].first;
                int str_size = cli_strs[i].second;
                if (str_size > site.GetRemainBandwidth()) {
                    break;
                }
                if (str_size == 0) {
                    goto next_round;
                }
                // client_demands_cpy[day][cli_idx] = 0;
                // it->second[cli_idx] = 0;
                // site.DecreaseBandwidth(str_size);
                auto s = Stream(cli_idx, max_site_idx, it->first, str_size);
                site.AddStream(s);
                clients_[cli_idx].AddStreamBySiteIndex(max_site_idx, s);
                it->second[cli_idx] = 0;
            }
            if (i >= 0) {
                for (int j = 0; j < i; j++) {
                    size_t cli_idx = cli_strs[j].first;
                    int str_size = cli_strs[j].second;
                    if (str_size > site.GetRemainBandwidth()) {
                        goto site_full;
                    }
                    if (str_size == 0) {
                        continue;
                    }
                    auto s = Stream(cli_idx, max_site_idx, it->first, str_size);
                    site.AddStream(s);
                    clients_[cli_idx].AddStreamBySiteIndex(max_site_idx, s);
                    it->second[cli_idx] = 0;
                }
            }
        next_round:;
        }
    site_full:

        site.IncFullTimes();
        site.SetFullThisTime();
    }
}

void SystemManager::BaseAllocate(Demand &d) {
    auto &need = d.GetStreamDemands();
    vector<pair<DemandIter, int>> sums;
    sums.reserve(need.size());
    for (auto it = need.begin(); it != need.end(); it++) {
        int sumv = accumulate(it->second.begin(), it->second.end(), 0);
        sums.push_back({it, sumv});
    }
    std::sort(sums.begin(), sums.end(),
              [](const pair<DemandIter, int> &l, const pair<DemandIter, int> &r) { return l.second > r.second; });

    set<size_t> sites;
    for (auto &p : sums) {
        auto it = p.first;
        sites.clear();
        for (size_t site_idx = 0; site_idx < sites_.size(); site_idx++) {
            if (sites_[site_idx].IsFullThisTime()) {
                continue;
            }
            sites.insert(site_idx);
        }
        while (!sites.empty()) {
            int best_grade = 0;
            int best_site = -1;
            for (size_t site_idx : sites) {
                int grade = 0;
                for (size_t cli_idx : sites_[site_idx].GetRefClients()) {
                    grade += it->second[cli_idx];
                }
                if (grade > best_grade) {
                    best_grade = grade;
                    best_site = site_idx;
                }
            }
            if (best_site == -1)
                break;
            if (best_grade <= sites_[best_site].GetSeperateBandwidth() - sites_[best_site].GetAllocatedBandwidth()) {
                for (size_t cli_idx : sites_[best_site].GetRefClients()) {
                    if (it->second[cli_idx] == 0)
                        continue;
                    auto s = Stream(cli_idx, best_site, it->first, it->second[cli_idx]);
                    sites_[best_site].AddStream(s);
                    clients_[cli_idx].AddStreamBySiteIndex(best_site, s);
                    it->second[cli_idx] = 0;
                }
            }
            sites.erase(best_site);
        }
    }
}

void SystemManager::AverageAllocate(Demand &d) {
    auto &need = d.GetStreamDemands();
    vector<Stream> streams;
    for (auto it = need.begin(); it != need.end(); it++) {
        for (size_t cli_idx = 0; cli_idx < it->second.size(); cli_idx++) {
            if (it->second[cli_idx] == 0) {
                continue;
            }
            streams.push_back(Stream{cli_idx, 0, it->first, it->second[cli_idx]});
        }
    }
    sort(streams.begin(), streams.end(),
         [](const Stream &l, const Stream &r) { return l.stream_size > r.stream_size; });
    for (auto &str : streams) {
        size_t cli_idx = str.cli_idx;
        auto &cli = clients_[cli_idx];
        auto &site_indexes = cli.GetAccessibleSite();
        string stream_name = str.stream_name;
        if (str.stream_size == 0) {
            continue;
        }
        bool flag = false;
        int min_site = -1;
        long min_grade = numeric_limits<long>::max();
        long grade = 0;
        int used = 0;
        int sep = 0;
        for (size_t site_idx : site_indexes) {
            auto &site = sites_[site_idx];
            if (site.GetRemainBandwidth() < str.stream_size) {
                continue;
            }
            used = site.GetAllocatedBandwidth() + str.stream_size;
            sep = site.GetSeperateBandwidth();
            if (used <= sep) {
                min_site = site_idx;
                min_grade = -1;
                flag = true;
                break;
            }
            grade = (used * used - sep * sep - 2 * base_cost_ * (used - sep)) / (site.GetTotalBandwidth()) +
                    (used - sep) +
                    static_cast<int>(max(0, str.stream_size - site.GetMaxStream(str.stream_name)) * center_cost_);
            // printf("site: %ld, grade: %ld\n", site_idx, grade);
            if (grade <= min_grade) {
                min_site = site_idx;
                min_grade = grade;
                flag = true;
            }
        }
        // printf("min site: %d, min grade = %ld\n", min_site, min_grade);
        auto &site = sites_[min_site];
        // site.DecreaseBandwidth(v[C]);
        site.AddStream(Stream{cli_idx, static_cast<size_t>(min_site), stream_name, str.stream_size});
        site.ResetSeperateBandwidth();
        cli.AddStreamBySiteIndex(min_site,
                                 Stream{cli_idx, static_cast<size_t>(min_site), stream_name, str.stream_size});
        // v[cli_idx] = 0;
        assert(flag == true);
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
    auto start = chrono::high_resolution_clock::now();

    // SystemManager manager;
    SystemManager manager("/output/solution.txt");
    manager.Init();
    manager.Process();

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "time taken: " << duration.count() << " ms\n";
    return 0;
}
