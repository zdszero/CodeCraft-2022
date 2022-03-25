#pragma once

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

#include "client.hpp"
#include "site.hpp"

template <typename T> void print_vec(std::vector<T> &v) {
    for (T val : v) {
        std::cout << val << " ";
    }
    std::cout << std::endl;
}

class FileParser {
  public:
    FileParser() = default;
    ~FileParser() {
        if (demand_fp_ != nullptr) {
            fclose(demand_fp_);
        }
    }

    // 读取/data/site_bandwidth文件，添加到sites数组中
    void ParseSites(std::vector<Site> &sites) {
        FILE *fp = fopen(site_filename_.c_str(), "r");
        std::vector<int> res;
        char buf[30];
        int bandwidth;
        fscanf(fp, "%*[^\n]\n");
        size_t site_idx = 0;
        while (fscanf(fp, "%[^,],%d\n", buf, &bandwidth) != EOF) {
            std::string site_name(buf);
            site_name_map_[site_name] = site_idx;
            sites.push_back({site_name, bandwidth});
            site_idx++;
        }
        fclose(fp);
    }

    // 读取qos_contraint
    void ParseConfig(int &constraint) {
        FILE *fp = fopen(config_filename_.c_str(), "r");
        fscanf(fp, "%*[^\n]\n");
        fscanf(fp, "qos_constraint=%d\n", &constraint);
        fclose(fp);
    }

    // 读取/data/qos.csv文件，创建clients数组
    void ParseQOS(std::vector<Client> &clients, int qos_constraint) {
        FILE *fp = fopen(qos_filename_.c_str(), "r");
        char buf[30];
        int qos;
        fscanf(fp, "site_name");
        size_t cli_idx = 0;
        while (fscanf(fp, ",%[^,\r\n]", buf) == 1) {
            std::string client_name(buf);
            client_name_map_[client_name] = cli_idx;
            clients.push_back({client_name});
            cli_idx++;
        }
        fscanf(fp, "\n");
        // i表示site的下标
        for (;;) {
            fscanf(fp, "%[^,]", buf);
            std::string site_name(buf);
            size_t cur_site_idx = site_name_map_[site_name];
            // 向每个client中添加满足qos限制的服务器
            for (size_t j = 0; j < clients.size(); j++) {
                fscanf(fp, ",%d", &qos);
                if (qos < qos_constraint) {
                    clients[j].accessible_sites_.push_back(cur_site_idx);
                }
            }
            fscanf(fp, "\n");
            int c = fgetc(fp);
            if (c == EOF) {
                for (size_t j = 0; j < clients.size(); j++) {
                    clients[j].Init();
                }
                break;
            }
            ungetc(c, fp);
        }
        fclose(fp);
    }

    // 读取下一个时间戳的用户节点的需求
    bool ParseDemand(int client_count, std::vector<int> &demand) {
        if (demand_fp_ == nullptr) {
            demand_fp_ = fopen(demand_filename_.c_str(), "r");
        }
        char buf[30];
        demand.resize(client_count, 0);
        if (!flag) {
            flag = true;
            // ignore the first line
            fscanf(demand_fp_, "mtime");
            demand_cli_idx_.reserve(client_name_map_.size());
            for (size_t k = 0; k < client_count; k++){
                fscanf(demand_fp_, ",%[^,\r\n]", buf);
                std::string cli_name(buf);
                demand_cli_idx_.push_back(client_name_map_[cli_name]);
            }
            fscanf(demand_fp_, "\n");
        }
        int c = fgetc(demand_fp_);
        if (c == EOF) {
            return false;
        }
        ungetc(c, demand_fp_);
        fscanf(demand_fp_, "%*[^,]");
        for (int i = 0; i < client_count; i++) {
            int tmp;
            fscanf(demand_fp_, ",%d", &tmp);
            demand[demand_cli_idx_[i]] = tmp;
        }
        fscanf(demand_fp_, "\n");
        return true;
    }
    
    int GetDemandsCount() {
        FILE *fp = fopen(demand_filename_.c_str(), "r");
        int res = 0;
        int c;
        while ((c = fgetc(fp)) != EOF) {
            if (c == '\n') {
                res++;
            }
        }
        return res;
    }

  private:
    bool flag{false};
    std::unordered_map<std::string, size_t> site_name_map_;
    std::unordered_map<std::string, size_t> client_name_map_;
    std::vector<size_t> demand_cli_idx_;
    std::string site_filename_{"/data/site_bandwidth.csv"};
    std::string config_filename_{"/data/config.ini"};
    std::string qos_filename_{"/data/qos.csv"};
    std::string demand_filename_{"/data/demand.csv"};
    FILE *demand_fp_{nullptr};
};
