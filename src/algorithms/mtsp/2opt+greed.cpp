// Standalone CLI version of the 2opt+greed solver for mTSP.
// Reads input.txt (format: first line "n m", then n pairs "x y"), writes output.txt
// with a list of m routes. Reference implementation; the registered version used in
// the main experiment grid is in src/mtsp_greedy_2opt_solver.cpp.

#include <bits/stdc++.h>
using namespace std;

// Entry point: round-robin nearest-neighbor + 2-opt to convergence on each route.
int main() {
    ifstream fin("input.txt");
    ofstream fout("output.txt");
    int n, m;
    if (!(fin >> n >> m)) {
        cerr << "Ошибка чтения входного файла\n";
        return 1;
    }
    // Read city coordinates
    vector<double> x(n), y(n);
    for (int i = 0; i < n; ++i) {
        fin >> x[i] >> y[i];
    }

    // 1. Greedy assignment of cities to routes
    vector<vector<int>> routes(m);
    vector<int> currentPos(m, 0);      // current position of each salesman (starting from depot = 0)
    vector<char> visited(n, 0);
    visited[0] = 1;                    // depot is considered visited
    int remaining = n - 1;             // number of unvisited cities (excluding depot)

    // While there are unassigned cities, assign them one by one to salesmen
    while (remaining > 0) {
        for (int i = 0; i < m && remaining > 0; ++i) {
            // Find the nearest unvisited city for salesman i
            double bestDistSq = DBL_MAX;
            int bestCity = -1;
            int cur = currentPos[i];
            for (int city = 1; city < n; ++city) {
                if (!visited[city]) {
                    double dx = x[cur] - x[city];
                    double dy = y[cur] - y[city];
                    double distSq = dx*dx + dy*dy;
                    if (distSq < bestDistSq) {
                        bestDistSq = distSq;
                        bestCity = city;
                    }
                }
            }
            if (bestCity == -1) break;  // no available cities (should not happen when remaining > 0)
            // Assign the nearest city found to the current salesman
            routes[i].push_back(bestCity);
            visited[bestCity] = 1;
            currentPos[i] = bestCity;
            remaining--;
        }
    }

    // 2. Local improvement: 2-opt optimization within each route
    // Distance function between two vertices (Euclidean distance)
    auto dist = [&](int a, int b) {
        double dx = x[a] - x[b];
        double dy = y[a] - y[b];
        return sqrt(dx*dx + dy*dy);
    };

    // Apply 2-opt to each route
    for (int k = 0; k < m; ++k) {
        // Prepare a working list with the depot inserted at the start and end of the route
        vector<int> route = routes[k];
        route.insert(route.begin(), 0);  // depot at the start
        route.push_back(0);             // depot at the end
        bool improved = true;
        int routeSize = route.size();
        // Iteratively try to improve the route by performing 2-opt swaps
        while (improved) {
            improved = false;
            // Check all pairs of edges (i-1,i) and (j,j+1) in the route
            for (int i = 1; i < routeSize - 2; ++i) {
                for (int j = i + 1; j < routeSize - 1; ++j) {
                    // Lengths of the old edges
                    double dist_before = dist(route[i-1], route[i]) + dist(route[j], route[j+1]);
                    // Lengths of the new edges if a 2-opt swap is performed (reversing segment [i, j])
                    double dist_after  = dist(route[i-1], route[j]) + dist(route[i], route[j+1]);
                    if (dist_after + 1e-9 < dist_before) {
                        // Improvement found: reverse the order of cities between i and j inclusive
                        reverse(route.begin() + i, route.begin() + j + 1);
                        improved = true;
                    }
                }
            }
        }
        // Remove the depot nodes added at the start and end, save the improved sequence
        route.erase(route.begin());         // remove leading 0
        route.pop_back();                   // remove trailing 0
        routes[k] = route;
    }

    // 3. Compute final distances and write output
    double totalDistance = 0.0;
    fout.setf(std::ios::fixed);
    for (int k = 0; k < m; ++k) {
        // Print route k (with depot at start and end)
        fout << "Route " << k+1 << ": 0";
        for (int city : routes[k]) {
            fout << " -> " << city;
        }
        fout << " -> 0\n";
        // Compute the length of route k
        double routeDist = 0.0;
        int prev = 0;
        for (int city : routes[k]) {
            routeDist += dist(prev, city);
            prev = city;
        }
        routeDist += dist(prev, 0);  // return to depot
        totalDistance += routeDist;
    }
    fout << setprecision(2) << "Total distance: " << totalDistance;
    return 0;
}
