#include "../lib/file_parser.hpp"

#include <cassert>
#include <cstdio>

void test_parse_sites() {
    FileParser file_parser;
    std::vector<std::string> site_names;
    auto site_bandwidth =
	file_parser.ParseSites("/data/site_bandwidth.csv", site_names);
    assert(site_bandwidth.size() == site_names.size());
    for (size_t i = 0; i < site_names.size(); i++) {
	printf("%s,%d\n", site_names[i].c_str(), site_bandwidth[i]);
    }
}

void test_parse_config() {
    FileParser file_parser;
    auto constraint = file_parser.ParseConfig("../../data/config.ini");
    printf("%d\n", constraint);
}

void test_parse_qos() {
    FileParser file_parser;
    std::vector<std::string> clients;
    auto accessible_sites =
	file_parser.ParseQOS("../../data/qos.csv", clients, 400);
    assert(accessible_sites.size() == clients.size());
    size_t sz = clients.size();
    for (size_t i = 0; i < sz; i++) {
	printf("%zd: ", i);
	for (int j = 0; j < accessible_sites[i].size(); j++) {
	    printf("%d ", accessible_sites[i][j]);
	}
	printf("\n");
    }
}

void test_parse_demand() {
    FileParser file_parser;
    std::string filename("/data/demand.csv");
    std::vector<int> demand;
    file_parser.ParseDemand(filename, 10, demand);
    for (auto elem : demand) {
	printf("%d ", elem);
    }
    printf("\n");
    assert(file_parser.ParseDemand(filename, 10, demand));
    for (auto elem : demand) {
	printf("%d ", elem);
    }
    printf("\n");
    assert(file_parser.ParseDemand(filename, 10, demand));
    for (auto elem : demand) {
	printf("%d ", elem);
    }
    printf("\n");
}

int main() {
    /* test_parse_sites(); */
    /* test_parse_config(); */
    /* test_parse_qos(); */
    test_parse_demand();
    return 0;
}
