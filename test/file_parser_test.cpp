#include "../lib/file_parser.hpp"

#include <cassert>
#include <cstdio>

void test_parse_sites() {
    FileParser file_parser;
    std::vector<Site> sites;
    file_parser.ParseSites(sites);
    for (size_t i = 0; i < sites.size(); i++) {
	printf("%s,%d\n", sites[i].GetName(), sites[i].GetTotalBandwidth());
    }
}

void test_parse_config() {
    FileParser file_parser;
    int constraint;
    file_parser.ParseConfig(constraint);
    printf("%d\n", constraint);
}

void test_parse_qos() {
    FileParser file_parser;
    std::vector<Client> clients;
    file_parser.ParseQOS(clients, 400);
    for (size_t i = 0; i < clients.size(); i++) {
	printf("%zd: ", i);
	for (int j = 0; j < clients[i].GetSiteCount(); j++) {
	    printf("%d ", clients[i].GetSiteIndex(j));
	}
	printf("\n");
    }
}

void test_parse_demand() {
    FileParser file_parser;
    std::string filename("/data/demand.csv");
    std::vector<int> demand;
    file_parser.ParseDemand(10, demand);
    for (auto elem : demand) {
	printf("%d ", elem);
    }
    printf("\n");
    assert(file_parser.ParseDemand(10, demand));
    for (auto elem : demand) {
	printf("%d ", elem);
    }
    printf("\n");
    assert(file_parser.ParseDemand(10, demand));
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
