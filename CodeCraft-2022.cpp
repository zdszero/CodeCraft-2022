#include <cassert>

#include "file_parser.hpp"

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
    void Init() {
        file_parser_.ParseSites(sites_);
        file_parser_.ParseConfig(qos_constraint_);
        file_parser_.ParseQOS(clients_, qos_constraint_);
        // 计算每个site被多少client使用
        for (const auto &c : clients_) {
            for (const auto site_idx : c.GetAccessibleSite()) {
                sites_[site_idx].IncRefTimes();
            }
        }
        for (size_t i = 0; i < clients_.size(); i++) {
            double sum = 0;
            auto &client = clients_[i];
            for (size_t j = 0; j < client.GetSiteCount(); j++) {
                const auto &site = GetSite(i, j);
                sum += (1.0 / site.GetRefTimes() * site.GetTotalBandwidth());
            }
            for (size_t j = 0; j < client.GetSiteCount(); j++) {
                const auto &site = GetSite(i, j);
                client.SetRatio(
                    j, (1.0 / site.GetRefTimes() * site.GetTotalBandwidth()) /
                           sum);
            }
        }
        /* for (size_t i = 0; i < clients_.size(); i++) { */
        /*     double sum = 0; */
        /*     const auto &c = clients_[i]; */
        /*     for (size_t j = 0; j < c.GetSiteCount(); j++) { */
        /*         sum += c.GetRatio(j); */
        /*     } */
        /*     printf("%.2f\n", sum); */
        /* } */
    }

    // 获取第i个client的第j个边缘结点
    Site &GetSite(int i, int j) { return sites_[clients_[i].GetSiteIndex(j)]; }

    void Schedule(const std::vector<int> &demand) {
        for (size_t i = 0; i < demand.size(); i++) {
            // client node i
            auto &client = clients_[i];
            int cur_demand = demand[i];
            if (cur_demand == 0) {
                continue;
            }
            for (;;) {
                int total = 0;
                int tmp;
                size_t j;
                for (j = 0; j < client.GetSiteCount() - 1; j++) {
                    auto &site = GetSite(i, j);
                    tmp = static_cast<int>(client.GetRatio(j) * cur_demand);
                    if (tmp > site.GetRemainBandwidth()) {
                        tmp = site.GetRemainBandwidth();
                    }
                    client.SetSiteAllocation(j, tmp);
                    total += tmp;
                    site.DecreaseBandwith(tmp);
                }
                // the last one should be calculated by substraction
                auto &site = GetSite(i, j);
                tmp = cur_demand - total;
                if (tmp <= site.GetRemainBandwidth()) {
                    client.SetSiteAllocation(j, tmp);
                    site.DecreaseBandwith(tmp);
                    break;
                }
                client.SetSiteAllocation(j, site.GetRemainBandwidth());
                cur_demand = tmp - site.GetRemainBandwidth();
                site.DecreaseBandwith(site.GetRemainBandwidth());
            }
        }
        WriteSchedule();
    }

    void Process() {
        std::vector<int> demand;
        while (file_parser_.ParseDemand(clients_.size(), demand)) {
            Schedule(demand);
        }
    }

    void WriteSchedule() {
        for (size_t i = 0; i < clients_.size(); i++) {
            fprintf(output_fp_, "%s:", clients_[i].GetName());
            for (size_t j = 0; j < clients_[i].GetSiteCount(); j++) {
                int site_idx = clients_[i].GetSiteIndex(j);
                if (j > 0) {
                    fprintf(output_fp_, ",");
                }
                if (clients_[i].GetSiteAllocation(j) > 0) {
                    fprintf(output_fp_, "<%s,%d>", sites_[site_idx].GetName(),
                            clients_[i].GetSiteAllocation(j));
                }
            }
            fprintf(output_fp_, "\n");
        }
    }

  private:
    FILE *output_fp_{stdout};
    FileParser file_parser_;
    int qos_constraint_;
    std::vector<Site> sites_;
    std::vector<Client> clients_;
};

int main() {
    SystemManager manager("/output/solution.txt");
    manager.Init();
    manager.Process();
}
