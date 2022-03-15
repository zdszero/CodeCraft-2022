#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "client.hpp"
#include "site.hpp"

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
        while (fscanf(fp, "%[^,],%d\n", buf, &bandwidth) != EOF) {
            sites.push_back({std::string(buf), bandwidth});
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
        while (fscanf(fp, ",%[^,\r\n]", buf) == 1) {
            clients.push_back({std::string(buf)});
        }
        fscanf(fp, "\n");
        // i表示site的下标
        for (int i = 0;; i++) {
            fscanf(fp, "%*[^,]");
            // 向每个client中添加满足qos限制的服务器
            for (size_t j = 0; j < clients.size(); j++) {
                fscanf(fp, ",%d", &qos);
                if (qos < qos_constraint) {
                    clients[j].accessible_sites_.push_back(i);
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
    std::string site_filename_{"/data/site_bandwidth.csv"};
    std::string config_filename_{"/data/config.ini"};
    std::string qos_filename_{"/data/qos.csv"};
    std::string demand_filename_{"/data/demand.csv"};
    FILE *demand_fp_{nullptr};
};
