#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>

using namespace std;

template <typename T> void print_vec(vector<T> &v) {
    for (T val : v) {
        cout << val << " ";
    }
    cout << endl;
}

class Demand {
  friend class FileParser;
  friend class SystemManager;
  public:
    Demand() = default;
  private:
    string time_;
    unordered_map<string, vector<int>> demands_;
};
