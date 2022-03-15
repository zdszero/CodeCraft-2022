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
    site_bandwidth_ =
        file_parser_.ParseSites("/data/site_bandwidth.csv", site_names_);
    sites_count_ = site_names_.size();
    qos_constraint_ = file_parser_.ParseConfig("/data/config.ini");
    accessible_sites_ =
        file_parser_.ParseQOS("/data/qos.csv", client_names_, qos_constraint_);
    clients_count_ = client_names_.size();
    site_ref_times_.resize(sites_count_, 0);
    for (const auto &v : accessible_sites_) {
      for (int site : v) {
        site_ref_times_[site]++;
      }
    }
    /* for (int i = 0; i < site_ref_times_.size(); i++) { */
    /*   printf("%s: %d\n", site_names_[i].c_str(), site_ref_times_[i]); */
    /* } */
    ratios_.resize(clients_count_, std::vector<double>{});
    for (size_t i = 0; i < accessible_sites_.size(); i++) {
      double sum = 0;
      for (size_t j = 0; j < accessible_sites_[i].size(); j++) {
        int site_idx = accessible_sites_[i][j];
        sum += (1.0 / site_ref_times_[site_idx] * site_bandwidth_[site_idx]);
      }
      ratios_[i].reserve(accessible_sites_[i].size());
      for (size_t j = 0; j < accessible_sites_[i].size(); j++) {
        int site_idx = accessible_sites_[i][j];
        ratios_[i].push_back(
            (1.0 / site_ref_times_[site_idx] * site_bandwidth_[site_idx]) /
            sum);
      }
    }
    /* assert(ratios_.size() == accessible_sites_.size()); */
    /* for (size_t i = 0; i < ratios_.size(); i++) { */
    /*   double sum = 0; */
    /*   for (size_t j = 0; j < ratios_[i].size(); j++) { */
    /*     sum += ratios_[i][j]; */
    /*   } */
    /*   printf("%.2f\n", sum); */
    /* } */
  }

  void Schedule(const std::vector<int> &demand) {
    std::vector<int> remain_bandwidth(site_bandwidth_);
    std::vector<std::vector<int>> allocation_table(accessible_sites_);
    for (auto &v : allocation_table) {
      for (auto &e : v) {
        e = 0;
      }
    }
    for (size_t i = 0; i < demand.size(); i++) {
      // client node i
      int cur_demand = demand[i];
      if (cur_demand == 0) {
        continue;
      }
      for (;;) {
        size_t j = 0;
        int total = 0;
        int tmp;
        int site_idx;
        for (j = 0; j < allocation_table[i].size() - 1; j++) {
          site_idx = accessible_sites_[i][j];
          tmp = static_cast<int>(ratios_[i][j] * cur_demand);
          if (tmp > remain_bandwidth[site_idx]) {
            tmp = remain_bandwidth[site_idx];
            printf("site %s is full\n", site_names_[site_idx].c_str());
          }
          allocation_table[i][j] = tmp;
          total += tmp;
          remain_bandwidth[site_idx] -= tmp;
        }
        // the last one should be calculated by substraction
        site_idx = accessible_sites_[i][j];
        tmp = cur_demand - total;
        if (tmp <= remain_bandwidth[site_idx]) {
          allocation_table[i][j] = tmp;
          remain_bandwidth[site_idx] -= tmp;
          break;
        }
        allocation_table[i][j] = remain_bandwidth[site_idx];
        cur_demand = tmp - remain_bandwidth[site_idx];
        remain_bandwidth[site_idx] = 0;
      }
    }
    WriteSchedule(allocation_table);
  }

  void Process() {
    std::vector<int> demand;
    while (
        file_parser_.ParseDemand("/data/demand.csv", clients_count_, demand)) {
      assert(demand.size() == client_names_.size());
      Schedule(demand);
    }
  }

  void WriteSchedule(const std::vector<std::vector<int>> &allocation_table) {
    for (size_t i = 0; i < allocation_table.size(); i++) {
      fprintf(output_fp_, "%s:", client_names_[i].c_str());
      for (size_t j = 0; j < allocation_table[i].size(); j++) {
        int site_idx = accessible_sites_[i][j];
        if (j > 0) {
          fprintf(output_fp_, ",");
        }
        if (allocation_table[i][j] > 0) {
          fprintf(output_fp_, "<%s,%d>", site_names_[site_idx].c_str(),
                  allocation_table[i][j]);
        }
      }
      fprintf(output_fp_, "\n");
    }
  }

 private:
  FILE *output_fp_{stdout};
  FileParser file_parser_;
  int qos_constraint_;
  size_t sites_count_{0};
  size_t clients_count_{0};
  std::vector<int> site_bandwidth_;
  std::vector<std::string> site_names_;
  std::vector<std::string> client_names_;  // client names
  std::vector<std::vector<int>> accessible_sites_;
  std::vector<std::vector<double>> ratios_;
  std::vector<int> site_ref_times_;
};

int main() {
  SystemManager manager("/data/solution.txt");
  manager.Init();
  manager.Process();
}
