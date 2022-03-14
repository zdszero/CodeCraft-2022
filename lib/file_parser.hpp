#pragma once

#include <cstdio>
#include <string>
#include <vector>

struct Site {
  std::string name_;
  int bandwidth_;
  Site(const std::string &name, int bandwidth)
      : name_(name), bandwidth_(bandwidth) {}
};

class FileParser {
 public:
  FileParser() = default;
  ~FileParser() {
    if (demand_fp_ != nullptr) {
      fclose(demand_fp_);
    }
  }

  std::vector<int> ParseSites(const std::string &filename, std::vector<std::string> &sites_names) {
    FILE *fp = fopen(filename.c_str(), "r");
    std::vector<int> res;
    char buf[30];
    int bandwidth;
    fscanf(fp, "%*[^\n]\n");
    while (fscanf(fp, "%[^,],%d\n", buf, &bandwidth) != EOF) {
      res.push_back(bandwidth);
      sites_names.push_back(std::string(buf));
    }
    fclose(fp);
    return res;
  }

  int ParseConfig(const std::string &filename) {
    FILE *fp = fopen(filename.c_str(), "r");
    int res;
    fscanf(fp, "%*[^\n]\n");
    fscanf(fp, "qos_constraint=%d\n", &res);
    fclose(fp);
    return res;
  }

  std::vector<std::vector<int>> ParseQOS(const std::string &filename,
                                         std::vector<std::string> &client_names,
                                         int qos_constraint) {
    FILE *fp = fopen(filename.c_str(), "r");
    char buf[30];
    int qos;
    fscanf(fp, "site_name");
    while (fscanf(fp, ",%[^,\r\n]", buf) == 1) {
      client_names.push_back(std::string(buf));
    }
    fscanf(fp, "\n");
    std::vector<std::vector<int>> accessible_sites(client_names.size());
    for (int i = 0;; i++) {
      fscanf(fp, "%[^,]", buf);
      for (size_t j = 0; j < client_names.size(); j++) {
        fscanf(fp, ",%d", &qos);
        if (qos < qos_constraint) {
          accessible_sites[j].push_back(i);
        }
      }
      fscanf(fp, "\n");
      int c = fgetc(fp);
      if (c == EOF) {
        break;
      }
      ungetc(c, fp);
    }
    fclose(fp);
    return accessible_sites;
  }

  bool ParseDemand(const std::string &filename, int client_count, std::vector<int> &demand) {
    if (demand_fp_ == nullptr) {
      demand_fp_ = fopen(filename.c_str(), "r");
    }
    char timebuf[30];
    demand.resize(client_count, 0);
    if (!flag) {
      flag = true;
      // ignore the first line
      fscanf(demand_fp_, "%*[^\n]\n");
    }
    int c = fgetc(demand_fp_);
    if (c == EOF) {
      return false;
    }
    ungetc(c, demand_fp_);
    fscanf(demand_fp_, "%[^,]", timebuf);
    for (int i = 0; i < client_count; i++) {
      int tmp;
      fscanf(demand_fp_, ",%d", &tmp);
      demand[i] = tmp;
    }
    fscanf(demand_fp_, "\n");
    return true;
  }

 private:
  bool flag{false};
  FILE *demand_fp_{nullptr};
};
