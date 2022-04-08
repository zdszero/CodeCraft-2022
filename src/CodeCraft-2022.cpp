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
    unique_ptr<ResultSet> results_;
    vector<vector<int>> daily_full_site_indexes_;

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
    // 向服务器中添加客户
    for (size_t i = 0; i < clients_.size(); i++) {
        for (const auto site_idx : clients_[i].GetAccessibleSite()) {
            sites_[site_idx].AddRefClient(i);
        }
    }
    // 对于每一个服务器
    std::for_each(sites_.begin(), sites_.end(), [this](Site &site) {
        // 对服务器中的客户进行排序
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
    // 对客户进行排序
    std::sort(clients_.begin(), clients_.end(),
              [](const Client &l, const Client &r) {
                  return l.GetSiteCount() < r.GetSiteCount();
              });
    /* std::sort(clients_.begin(), clients_.end(), */
    /*           [this](const Client &l, const Client &r) { */
    /*               auto GetAvailable = [this](const Client &cli) -> int { */
    /*                   int ret = 0; */
    /*                   for (size_t site_idx : cli.GetAccessibleSite()) { */
    /*                       ret += sites_[site_idx].GetTotalBandwidth() / */
    /*                              sites_[site_idx].GetRefTimes(); */
    /*                   } */
    /*                   return ret; */
    /*               }; */
    /*               return GetAvailable(l) < GetAvailable(r); */
    /*           }); */
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
             [this](int l, int r) {
                 return sites_[l].GetRefTimes() < sites_[r].GetRefTimes();
             });
        /* sort(cli.GetAccessibleSite().begin(), cli.GetAccessibleSite().end(), */
        /*      [this](int l, int r) { */
        /*          auto RefClientsNeed = [this](int site_idx) -> long { */
        /*             long ret = 0; */
        /*             auto &site = sites_[site_idx]; */
        /*             for (size_t cli_idx : site.GetRefClients()) { */
        /*                 ret += clients_[cli_idx].GetAccessTotal(); */
        /*             } */
        /*             return ret; */
        /*          }; */
        /*          return RefClientsNeed(l) > RefClientsNeed(r); */
        /*      }); */
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
    results_ =
        unique_ptr<ResultSet>(new ResultSet(sites_, clients_, base_cost_));
    /* results_->Reserve(demands_.size()); */
    results_->Resize(demands_.size());

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
    bool operator()(const DailySite &a, const DailySite &b) {
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
    sort(max_site_indexes.begin(), max_site_indexes.end(),
         [this](size_t l, size_t r) {
             return
                 /* return sites_[l].GetTotalBandwidth() ==
                    sites_[r].GetTotalBandwidth() ? */
                 /*            (sites_[l].GetRefTimes() >
                    sites_[r].GetRefTimes()) : */
                 (sites_[l].GetTotalBandwidth() >
                  sites_[r].GetTotalBandwidth());
         });

    // 按照site容量降序计算在哪一天打满
    auto client_demands_cpy = client_demands_;
    daily_full_site_indexes_.resize(demands_.size(), vector<int>());
    vector<bool> usable(sites_.size(), true);
    for (size_t i = 0; i < sites_.size(); i++) {
        if (sites_[i].GetRefTimes() == 0) {
            usable[i] = false;
        }
    }
    for (size_t site_idx : max_site_indexes) {
        if (not usable[site_idx]) {
            continue;
        }
        std::priority_queue<DailySite, std::vector<DailySite>, DailySiteCmp>
            site_max_req;
        for (size_t day = 0; day < client_demands_cpy.size(); day++) {
            int cur_sum = 0;
            auto &day_demand = client_demands_cpy[day];
            for (size_t cli_idx : sites_[site_idx].GetRefClients()) {
                /* int div_cnt = 0; */
                /* for (size_t sidx: clients_[cli_idx].GetAccessibleSite()) { */
                /*     if (usable[sidx]) { */
                /*         div_cnt++; */
                /*     } */
                /* } */
                /* if (div_cnt > 0) { */
                /*     cur_sum += day_demand[cli_idx] / div_cnt; */
                /* } */
                cur_sum +=
                    day_demand[cli_idx] / clients_[cli_idx].GetSiteCount();
                /* cur_sum += day_demand[cli_idx]; */
                //((sites_[site_index].GetTotalBandwidth() * 1.0) /
                //(1.0 * clients_[cli_idex].GetAccessTotal()));
            }
            if (cur_sum > 0) {
                site_max_req.push({day, site_idx, cur_sum,
                                   sites_[site_idx].GetTotalBandwidth()});
            }
        }
        // 找出每个服务器打得最满的%5天
        for (int j = 0; j < (int)(demands_.size() * 0.05); j++) {
            if (site_max_req.empty()) {
                break;
            }
            DailySite daily_site = site_max_req.top();
            int day = daily_site.GetTime();
            int remainBandwidth = daily_site.GetRemainBandwidth();
            auto &day_demand = client_demands_cpy[day];
            for (size_t cli_idx :
                 sites_[daily_site.GetSiteIdx()].GetRefClients()) {
                if (day_demand[cli_idx] == 0) {
                    continue;
                }
                int allocated = std::min(remainBandwidth, day_demand[cli_idx]);
                // assert(clients_[c].SetAllocationBySite(max_site_idx,
                // allocated));
                day_demand[cli_idx] -= allocated;
                remainBandwidth -= allocated;
                if (remainBandwidth == 0) {
                    break;
                }
            }
            daily_full_site_indexes_[day].push_back(daily_site.GetSiteIdx());
            site_max_req.pop();
        }
        usable[site_idx] = false;
    }
}

void SystemManager::Process() {
    PresetMaxSites();
    // 对访问demand的顺序进行排序
    vector<size_t> days;
    for (size_t day_idx = 0; day_idx < demands_.size(); day_idx++) {
        days.push_back(day_idx);
    }

    //    auto demands_copy = demands_;
    //    for (int idx = 0; idx < demands_.size(); idx++) {
    //        auto &d = demands_copy[idx];
    //        for (auto &site : sites_) {
    //            site.Reset();
    //        }
    //        for (auto &client : clients_) {
    //            client.Reset();
    //        }
    //        GreedyAllocate(d, idx);
    //    }
    //
    //    for (auto &site : sites_) {
    //        site.Reset();
    //    }
    //    for (auto &client : clients_) {
    //        client.Reset();
    //    }
    sort(days.begin(), days.end(), [this](size_t l, size_t r) {
        return demands_[l].GetTotalDemand() < demands_[r].GetTotalDemand();
    });

    //    sort(days.begin(), days.end(),
    //         [&demands_copy](size_t l, size_t r) {
    //             return demands_copy[l].GetTotalDemand() >
    //                     demands_copy[r].GetTotalDemand();
    //         });
    for (size_t day_idx : days) {
        /* for (size_t day_idx = 0; day_idx < demands_.size(); day_idx++) { */
        auto &d = demands_[day_idx];
        Schedule(d, day_idx);
    }
    for (size_t times = 0; times < 20; times++) {
        results_->Migrate();
    }
    results_->ExpelTop5();
    /* results_->UpdateTop5(); */
    /* results_->ExpelTop5(); */
    /* for (auto &site : sites_) { */
    /*     site.PrintClients(); */
    /* } */
    /* for (auto &cli : clients_) { */
    /*     cli.PrintSites(); */
    /* } */
    printf("grade = %d\n", results_->GetGrade());
    /* results_->PrintLoads(); */
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
    results_->SetResult(day, Result(day, clients_, sites_));
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
        /*
        vector<Stream> streams;
        for (auto it = need.begin(); it != need.end(); it++) {
            for (size_t cli_idx : site.GetRefClients()) {
                streams.push_back(
                    Stream{cli_idx, 0, it->first, it->second[cli_idx]});
            }
        }
        sort(streams.begin(), streams.end(),
             [](const Stream &l, const Stream &r) {
                 return l.stream_size > r.stream_size;
             });
        for (auto &s : streams) {
            if (s.stream_size > site.GetRemainBandwidth()) {
                continue;
            }
            if (s.stream_size == 0) {
                continue;
            }
            s.site_idx = max_site_idx;
            site.AddStream(s);
            clients_[s.cli_idx].AddStreamBySiteIndex(max_site_idx, s);
            need[s.stream_name][s.cli_idx] = 0;
        }
        */
        for (size_t cli_idx : site.GetRefClients()) {
            using ittype = decltype(need.begin());
            vector<ittype> its;
            for (auto it = need.begin(); it != need.end(); it++) {
                its.push_back(it);
            }
            sort(its.begin(), its.end(), [cli_idx](ittype l, ittype r) {
                return l->second[cli_idx] > r->second[cli_idx];
            });
            for (auto it : its) {
                /* for (auto it = need.begin(); it != need.end(); it++) { */
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
                if (allocated + v[cli_idx] >= sep) {
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
    for (size_t cli_idx = 0; cli_idx < clients_.size(); cli_idx++) {
        auto &cli = clients_[cli_idx];
        auto &site_indexes = cli.GetAccessibleSite();
        for (auto it = need.begin(); it != need.end(); it++) {
            string stream_name = it->first;
            auto &v = it->second;
            if (v[cli_idx] == 0) {
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
                if (site.GetRemainBandwidth() < v[cli_idx]) {
                    continue;
                }
                used = site.GetAllocatedBandwidth() + v[cli_idx];
                sep = site.GetSeperateBandwidth();
                if (used <= sep) {
                    min_site = site_idx;
                    min_grade = -1;
                    flag = true;
                    break;
                }
                grade = /*pow((used - base_cost_), 2) * 1.0
                        / (site.GetTotalBandwidth() * 1.0) + used;*/
                    (used * used - sep * sep - 2 * base_cost_ * (used - sep)) /
                        (site.GetTotalBandwidth()) +
                    (used - sep);
                /* printf("site: %ld, grade: %ld\n", site_idx, grade); */
                if (grade <= min_grade) {
                    min_site = site_idx;
                    min_grade = grade;
                    flag = true;
                }
            }
            /* printf("min site: %d, min grade = %ld\n", min_site, min_grade);
             */
            auto &site = sites_[min_site];
            /* site.DecreaseBandwidth(v[C]); */
            site.AddStream(Stream{cli_idx, static_cast<size_t>(min_site),
                                  stream_name, v[cli_idx]});
            site.ResetSeperateBandwidth();
            cli.AddStreamBySiteIndex(
                min_site, Stream{cli_idx, static_cast<size_t>(min_site),
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
    auto start = chrono::high_resolution_clock::now();

    /* SystemManager manager; */
    SystemManager manager("/output/solution.txt");
    manager.Init();
    manager.Process();

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "time taken: " << duration.count() << " ms\n";
    return 0;
}
