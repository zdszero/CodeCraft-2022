#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

#include "site.hpp"

class CenterResult {
    friend class CenterResultSet;
public:
    CenterResult() = default;
    void Init(const vector<Site> &sites) {
        load_ = 0;
        for (const auto &site : sites) {
            for (auto it = site.stream_max_.begin(); it != site.stream_max_.end(); it++) {
                load_ += it->second;
            }
        }
    }

private:
    int load_{0};
};

class CenterResultSet {
public:
    CenterResultSet() = default;
    void Resize(size_t n) {
        res_.resize(n);
        grades_.resize(n);
    }
    void SetResult(size_t day, const vector<Site> &sites) {
        res_[day].Init(sites);
        grades_[day] = res_[day].load_;
    }
    int GetGrade() {
        auto g = grades_;
        sort(g.begin(), g.end());
        size_t sep_idx = ceil(g.size() * 0.95) - 1;
        return g[sep_idx];
    }

private:
    vector<CenterResult> res_;
    vector<int> grades_;
};
