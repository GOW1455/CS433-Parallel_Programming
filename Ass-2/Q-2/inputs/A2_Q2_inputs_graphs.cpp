#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include<bits/stdc++.h>

using namespace std;

int main(int argc, char* argv[])
{
    if(argc < 3)
    {
        cout << "Usage: " << argv[0] << " <no_of_nodes> <no_of_edges>" << endl;
        return 1;
    }
    int n = atoi(argv[1]);
    int m = atoi(argv[2]);

    long long max = 1LL * n * (n - 1);
    if(m > max)
    {
        cerr << "no of edges is too large" << '\n';
        return 1;
    }
    string filename = "A2_Q2_graph_N=" + to_string(n) + "_M="+ to_string(m) + ".txt";

    ofstream outfile(filename);
    if (!outfile.is_open()) {
        cerr << "Error opening file: " << filename << endl;
        return 1;
    }

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> distrib(0, n - 1);

    outfile << n << " " << m << '\n';

    set<pair<int, int>> edges;

    while(edges.size()<m)
    {
        int u = distrib(gen);
        int v = u;
        while(v==u) v = distrib(gen);
        edges.insert({u,v});
    }

    for(auto edge:edges)
    {
        outfile << edge.first << " " << edge.second << '\n';
    }
    outfile.close();

    return 0;
}