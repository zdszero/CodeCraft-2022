#include "../lib/set_comparator.hpp"

template <typename T> void print_vec(std::vector<T> &v) {
    for (T val : v) {
        std::cout << val << " ";
    }
    std::cout << std::endl;
}

int main() {
    std::vector<Site> sites;
    sites.reserve(3);
    sites.push_back(Site("A", 100, {1, 2, 3, 4, 5}));
    sites.push_back(Site("B", 100, {4, 5, 6}));
    sites.push_back(Site("C", 100, {6, 7, 8, 9}));
    sites.push_back(Site("D", 100, {1, 6}));
    sites.push_back(Site("E", 100, {6, 8, 10}));
    sites.push_back(Site("F", 100, {10, 15, 19, 22}));
    sites.push_back(Site("G", 100, {12, 18, 8, 22}));
    sites.push_back(Site("H", 100, {5, 9, 19, 28, 33}));
    SetCompartor set_comparator(sites);
    set_comparator.Print();
    auto v = set_comparator.IrrelevantSites(2, 5);
    print_vec(v);
    return 0;
}
