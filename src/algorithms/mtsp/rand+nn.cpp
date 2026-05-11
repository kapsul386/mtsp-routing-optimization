// Standalone CLI version of the rand+nn solver for mTSP.
// Randomly shuffles clients, assigns them round-robin to m salesmen,
// then builds a route for each group using a greedy nearest-neighbor heuristic.
// Reference implementation; the registered version used in the main experiment grid is in
// src/mtsp_rand_nn_solver.cpp.

#include <bits/stdc++.h>
using namespace std;

// Entry point: reads input.txt, performs randomized-greedy assignment and nearest-neighbor routing.
int main() {
    // Open input and output files.
    ifstream fin("input.txt");
    ofstream fout("output.txt");
    int n, m;
    if (!(fin >> n >> m)) {
        cerr << "Ошибка чтения входных данных\n";
        return 1;
    }
    // Read city coordinates (city 0 is the depot)
    vector<double> x(n), y(n);
    for (int i = 0; i < n; ++i) {
        fin >> x[i] >> y[i];
    }

    // 1. Random assignment of cities to salesmen
    vector<int> cities;
    for (int city = 1; city < n; ++city) {
        cities.push_back(city);
    }
    srand(time(NULL));                  // seed the random number generator
    random_shuffle(cities.begin(), cities.end());  // randomly shuffle the city list

    // Partition the city list into M routes (approximately equal size)
    vector<vector<int>> routes(m);
    for (size_t idx = 0; idx < cities.size(); ++idx) {
        int salesman = idx % m;
        routes[salesman].push_back(cities[idx]);
    }

    // 2. Build a route for each salesman using the nearest-neighbor heuristic
    for (int k = 0; k < m; ++k) {
        int current = 0;                      // current position (start from depot = 0)
        vector<int> orderedRoute;             // new ordered route
        vector<bool> used(routes[k].size());  // visited flags for cities in this route

        // While there are unordered cities remaining for this salesman
        for (int count = routes[k].size(); count > 0; --count) {
            // Find the nearest unused city from the current position
            double bestDist = DBL_MAX;
            int bestIndex = -1;
            for (int j = 0; j < (int)routes[k].size(); ++j) {
                if (!used[j]) {
                    int city = routes[k][j];
                    double dx = x[current] - x[city];
                    double dy = y[current] - y[city];
                    double distSq = dx*dx + dy*dy;  // squared distance (for comparison)
                    if (distSq < bestDist) {
                        bestDist = distSq;
                        bestIndex = j;
                    }
                }
            }
            // Add the found city to the route
            used[bestIndex] = true;
            current = routes[k][bestIndex];
            orderedRoute.push_back(current);
        }
        // Replace the original unordered list with the ordered route
        routes[k] = orderedRoute;
    }

    // 3. Compute distances and format output
    double totalDistance = 0.0;
    fout.setf(std::ios::fixed);
    // Print the route for each salesman
    for (int k = 0; k < m; ++k) {
        // Print route: start at depot (0), then the sequence of cities, then return to 0
        fout << "Route " << k+1 << ": 0";
        for (int city : routes[k]) {
            fout << " -> " << city;
        }
        fout << " -> 0\n";
        // Compute the length of route k
        double routeDist = 0.0;
        int prev = 0;  // start from depot
        for (int city : routes[k]) {
            double dx = x[prev] - x[city];
            double dy = y[prev] - y[city];
            routeDist += sqrt(dx*dx + dy*dy);
            prev = city;
        }
        // add the distance from the last city back to the depot
        double dx = x[prev] - x[0];
        double dy = y[prev] - y[0];
        routeDist += sqrt(dx*dx + dy*dy);
        totalDistance += routeDist;
    }
    // Print the total length of all routes (two decimal places)
    fout << setprecision(2) << "Total distance: " << totalDistance;
    return 0;
}
