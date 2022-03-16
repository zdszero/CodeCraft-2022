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

// std::make_unique is upcoming in c++ 14
template <typename T, typename... Args>
std::unique_ptr<T> MakeUnique(Args &&...args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

class SetCompartor {
  public:
    SetCompartor() = default;
    SetCompartor(const std::vector<Site> &sites) {
        size_ = sites.size();
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
        assert(count < size_);
        vector<int> res;
        res.reserve(count);
        vector<std::pair<int, double>> pairs;
        for (size_t j = 0; j < size_; j++) {
            pairs.push_back({j, GetElement(i, j)});
        }
        std::sort(pairs.begin(), pairs.end(),
                  [](const std::pair<int, double> &lhs,
                     const std::pair<int, double> &rhs) {
                      return (lhs.second < rhs.second)
                                 ? true
                                 : (lhs.first < rhs.first);
                  });
        for (int i = 1; i < 1 + count; i++) {
            res.push_back(pairs[i].first);
        }
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

    double SetSimilarity(const vector<int> &l, const vector<int> &r) {
        std::vector<int> union_set;
        std::vector<int> intersect_set;
        std::set_union(l.begin(), l.end(), r.begin(), r.end(),
                       std::back_inserter(union_set));
        std::set_intersection(l.begin(), l.end(), r.begin(), r.end(),
                              std::back_inserter(intersect_set));
        return (1.0 * intersect_set.size() / union_set.size());
    }

    double GetElement(size_t i, size_t j) const { return (*matrix_)[i][j]; }
    void SetElement(size_t i, size_t j, double val) { (*matrix_)[i][j] = val; }
};
