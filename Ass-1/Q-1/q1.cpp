// CS433 Assignment 1, Q1: exact search, parallel INSIDE each k (OpenMP).
// Compile: g++ -O2 -std=c++11 -fopenmp q1_parallel.cpp -o q1_parallel
// Run:     ./q1_parallel input.txt output.txt threads
//
// Idea: ask "is k colors enough?" for k = lowerBound, lowerBound+1, ...
// The first feasible k is optimal. For each k the search tree is cut at a
// shallow depth into many independent subtrees (prefix colorings of the first
// d objects). Threads take subtrees dynamically, so even the long
// "k colors are impossible" proof (which must visit the whole tree) is shared.
// A shared flag stops everyone as soon as one thread finds a coloring.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <climits>
#include <atomic>
#include <omp.h>
using namespace std;

struct Graph {
    int n;
    vector<pair<int, int>> edges;
    vector<vector<pair<int, int>>> adj; // (neighbor, edge number)
};

bool readGraph(const string& filename, Graph& g) {
    ifstream in(filename);
    string line, extra;
    if (!getline(in, line)) return false;
    stringstream first(line);
    if (!(first >> g.n) || g.n < 0 || (first >> extra)) return false;
    g.adj.resize(g.n);
    for (int u = 0; u < g.n - 1; u++) {
        if (!getline(in, line)) return false;
        stringstream row(line);
        vector<int> neighbors;
        int v;
        while (row >> v) {
            if (v <= u || v >= g.n) return false;
            neighbors.push_back(v);
        }
        if (!row.eof()) return false;
        sort(neighbors.begin(), neighbors.end());
        for (int i = 0; i < (int)neighbors.size(); i++) {
            v = neighbors[i];
            if (i > 0 && v == neighbors[i - 1]) return false;
            if (g.edges.size() >= (size_t)(INT_MAX - g.n)) return false;
            int e = g.edges.size();
            g.edges.push_back({u, v});
            g.adj[u].push_back({v, e});
            g.adj[v].push_back({u, e});
        }
    }
    while (getline(in, line))
        if (line.find_first_not_of(" \t\r") != string::npos) return false;
    for (int u = 0; u < g.n; u++) sort(g.adj[u].begin(), g.adj[u].end());
    return true;
}

// Objects 0..n-1 are vertices, object n+e is edge e. Rows are sorted.
vector<vector<int>> buildConflictGraph(const Graph& g, int threads) {
    int m = g.edges.size();
    vector<vector<int>> conflict(g.n + m);
    #pragma omp parallel for num_threads(threads) schedule(dynamic, 64)
    for (int object = 0; object < g.n + m; object++) {
        vector<int>& row = conflict[object];
        if (object < g.n) {
            for (auto nb : g.adj[object]) {
                row.push_back(nb.first);
                row.push_back(g.n + nb.second);
            }
        } else {
            int e = object - g.n;
            int u = g.edges[e].first, v = g.edges[e].second;
            row.push_back(u);
            row.push_back(v);
            int i = 0, j = 0;
            while (i < (int)g.adj[u].size() && j < (int)g.adj[v].size()) {
                int a = g.adj[u][i].first, b = g.adj[v][j].first;
                if (a < b) i++;
                else if (a > b) j++;
                else {
                    row.push_back(g.n + g.adj[u][i].second);
                    row.push_back(g.n + g.adj[v][j].second);
                    i++; j++;
                }
            }
        }
        sort(row.begin(), row.end());
    }
    return conflict;
}



inline bool canUseColor(int v, int c, const vector<vector<int>>& conflict,
                        const int* color) {
    for (int w : conflict[v])
        if (color[w] == c) return false;
    return true;
}

// Cut the tree at depth d: list every symmetry-broken prefix coloring of
// order[0..d-1]. Grows d until there are >= target subtrees.
int makePrefixes(int k, const vector<vector<int>>& conflict,
                 const vector<int>& order, size_t target,
                 vector<vector<int>>& prefixes) {
    int N = order.size();
    vector<int> color(N, -1);
    prefixes.assign(1, vector<int>());
    int d = 0;
    while (prefixes.size() < target && d < N) {
        vector<vector<int>> next;
        for (auto& p : prefixes) {
            int usedLabels = 0;
            for (int i = 0; i < d; i++) {
                color[order[i]] = p[i];
                usedLabels = max(usedLabels, p[i] + 1);
            }
            for (int c = 0; c < k && c <= usedLabels; c++)
                if (canUseColor(order[d], c, conflict, color.data())) {
                    next.push_back(p);
                    next.back().push_back(c);
                }
            for (int i = 0; i < d; i++) color[order[i]] = -1;
        }
        prefixes.swap(next);
        d++;
        if (prefixes.empty()) break;
    }
    return d;
}

// Finish one subtree (depth >= d). Returns true if a full coloring exists;
// then color[] holds it. Stops early (false) if another thread already won.
bool searchSubtree(int k, int d, const vector<int>& prefix,
                   const vector<vector<int>>& conflict,
                   const vector<int>& order, vector<int>& color,
                   vector<int>& used, const atomic<int>& found) {
    int N = order.size();
    used[0] = 0;
    for (int i = 0; i < d; i++) {
        color[order[i]] = prefix[i];
        used[i + 1] = max(used[i], prefix[i] + 1);
    }
    int depth = d;
    unsigned steps = 0;
    while (depth >= d) {
        if (((++steps) & 1023) == 0 && found.load(memory_order_relaxed))
            return false;                    // abandon; caller stops anyway
        if (depth == N) return true;
        int v = order[depth];
        int c = color[v] + 1;
        color[v] = -1;
        for (; c < k && c <= used[depth]; c++)
            if (canUseColor(v, c, conflict, color.data())) break;
        if (c < k && c <= used[depth]) {
            color[v] = c;
            used[depth + 1] = max(used[depth], c + 1);
            depth++;
        } else {
            depth--;
        }
    }
    for (int i = 0; i < d; i++) color[order[i]] = -1;   // clean for next task
    return false;
}

bool solveForK(int k, int threads, const vector<vector<int>>& conflict,
               const vector<int>& order, vector<vector<int>>& colors,
               vector<vector<int>>& useds, vector<int>& answer) {
    vector<vector<int>> prefixes;
    int d = makePrefixes(k, conflict, order, (size_t)threads * 4, prefixes);
    if (prefixes.empty()) return false;
    atomic<int> found(0);
    int numTasks = prefixes.size();
    #pragma omp parallel num_threads(threads)
    {
        int id = omp_get_thread_num();
        vector<int>& color = colors[id];
        vector<int>& used = useds[id];
        #pragma omp for schedule(dynamic, 1)
        for (int t = 0; t < numTasks; t++) {
            if (found.load(memory_order_relaxed)) continue;
            if (searchSubtree(k, d, prefixes[t], conflict, order, color,
                              used, found)) {
                #pragma omp critical(answer_write)
                {
                    if (!found.load()) {
                        answer = color;
                        found.store(1);
                    }
                }
            }
        }
    }
    return found.load() == 1;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Usage: q1_parallel input.txt output.txt threads\n";
        return 1;
    }
    int threads;
    string extra;
    stringstream argument(argv[3]);
    if (!(argument >> threads) || threads < 1 || (argument >> extra)) {
        cerr << "Thread count must be a positive integer.\n";
        return 1;
    }
    Graph g;
    if (!readGraph(argv[1], g)) {
        cerr << "Cannot read the graph. Check the input file and its format.\n";
        return 1;
    }
    int m = g.edges.size();
    int total = g.n + m;
    omp_set_dynamic(0);

    // Allocation / setup (not part of the timed search below).
    vector<int> order;
    order.reserve(total);
    for (int u = 0; u < g.n; u++) {
        order.push_back(u);
        for (auto nb : g.adj[u])
            if (nb.first < u) order.push_back(g.n + nb.second);
    }
    vector<vector<int>> colors(threads, vector<int>(total, -1));
    vector<vector<int>> useds(threads, vector<int>(total + 1, 0));
    vector<int> answer(total, 0);

    double t0 = omp_get_wtime();
    vector<vector<int>> conflict = buildConflictGraph(g, threads);
    int best = 0;
    if (total > 0) {
        for (int k = 1; k <= total; k++) {
            #ifdef TASKLOG
            double start_k = omp_get_wtime();
            #endif
            bool ok = solveForK(k, threads, conflict, order, colors, useds, answer);
            #ifdef TASKLOG
            cerr << "K " << k << " " << (ok ? "SAT" : "UNSAT")
                 << " " << omp_get_wtime() - start_k << "\n";
            #endif
            if (ok) { best = k; break; }
        }
    }
    double t1 = omp_get_wtime();
    cerr << " THREADS " << threads
         << " SOLVE_TIME " << t1 - t0 << '\n';

    ofstream out(argv[2]);
    if (!out) {
        cerr << "Cannot open the output file.\n";
        return 1;
    }
    for (int u = 0; u < g.n; u++) out << u << ", " << answer[u] << '\n';
    for (int e = 0; e < m; e++)
        out << "(" << g.edges[e].first << ", " << g.edges[e].second << "), "
            << answer[g.n + e] << '\n';
    out << best << '\n';
    out.close();
    if (!out) {
        cerr << "Could not write the output.\n";
        return 1;
    }
    return 0;
}
