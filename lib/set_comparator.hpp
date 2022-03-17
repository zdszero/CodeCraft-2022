#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "site.hpp"

using std::vector;
using matrix_t = std::vector<std::vector<double>>;

// std::make_unique is not introduced in c++11
template <typename T, typename... Args>
std::unique_ptr<T> MakeUnique(Args &&...args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

class SetCompartor {
  public:
    SetCompartor() = default;
    SetCompartor(const std::vector<Site> &sites) {
        size_ = sites.size();
        sites_ = &sites;
        matrix_ = MakeUnique<matrix_t>(
            vector<vector<double>>(size_, vector<double>(size_, 0)));
        for (size_t i = 0; i < sites.size(); i++) {
            for (size_t j = i + 1; j < sites.size(); j++) {
                double val = SetSimilarity(sites[i].GetRefClients(),
                                           sites[j].GetRefClients());
                SetElement(i, j, val);
                SetElement(j, i, val);
            }
        }
    }
    // get the count most irrelevant sites
    vector<int> IrrelevantSites(int i, int count) const {
        vector<int> res;
        dfs(i, count, res);
        return res;
    }

    void Print() const {
        for (const auto &row : (*matrix_)) {
            for (const auto &s : row)
                std::cout << s << ' ';
            std::cout << std::endl;
        }
    }

  private:
    std::unique_ptr<matrix_t> matrix_{nullptr};
    size_t size_{0};
    const std::vector<Site> *sites_{nullptr};

    double SetSimilarity(const vector<int> &l, const vector<int> &r) {
        std::vector<int> union_set;
        std::vector<int> intersect_set;
        std::set_union(l.begin(), l.end(), r.begin(), r.end(),
                       std::back_inserter(union_set));
        std::set_intersection(l.begin(), l.end(), r.begin(), r.end(),
                              std::back_inserter(intersect_set));
        return (1.0 * intersect_set.size() / union_set.size());
    }

    inline double GetElement(size_t i, size_t j) const {
        return (*matrix_)[i][j];
    }
    inline void SetElement(size_t i, size_t j, double val) {
        (*matrix_)[i][j] = val;
    }
    void dfs(int i, int count, vector<int> &res) const {
        res.push_back(i);
        if (res.size() >= static_cast<size_t>(count)) {
            return;
        }
        int min_idx = -1;
        double min_val = 1.1;
        for (int j = 0; j < static_cast<int>(matrix_->at(i).size()); j++) {
            // find the smallest value
            if (i == j || !sites_->at(j).IsSafe() ||
                std::find(res.begin(), res.end(), j) != res.end()) {
                continue;
            }
            if (GetElement(i, j) < min_val) {
                min_val = GetElement(i, j);
                min_idx = j;
            }
        }
        assert(min_idx != -1);
        dfs(min_idx, count, res);
    }
};
