// Standalone CLI version of the GRASP solver for mTSP.
// Implements the Greedy Randomised Adaptive Search Procedure: each iteration (1) builds
// a randomized-greedy solution via a Restricted Candidate List, (2) improves it with local search.
// Reference implementation; the registered version used in the main experiment grid is in
// src/mtsp_grasp_solver.cpp.

#include <bits/stdc++.h>
using namespace std;

// Compute the Euclidean distance between two points (cities).
double distEuclidean(const pair<double,double>& a, const pair<double,double>& b) {
    double dx = a.first - b.first;
    double dy = a.second - b.second;
    return sqrt(dx*dx + dy*dy);
}

int main() {
    ifstream fin("input.txt");
    ofstream fout("output.txt");
    int n, m;
    if (!(fin >> n >> m)) {
        cerr << "Ошибка чтения входных данных\n";
        return 1;
    }
    // Read city coordinates (city 0 is the depot)
    vector<pair<double,double>> coords(n);
    for (int i = 0; i < n; ++i) {
        fin >> coords[i].first >> coords[i].second;
    }
    srand(time(NULL));  // seed the RNG

    double bestDistance = DBL_MAX;
    vector<vector<int>> bestRoutes;  // best solution found so far

    int maxIterations = 50;  // number of GRASP iterations (tunable)
    for (int iter = 0; iter < maxIterations; ++iter) {
        // *** Phase 1: Build a randomized-greedy initial solution ***
        vector<char> visited(n, 0);
        visited[0] = 1;                    // depot is visited by all
        int remaining = n - 1;
        vector<vector<int>> routes(m);
        vector<int> currentPos(m, 0);      // current position of each salesman (city index)

        // Assign all cities to routes
        while (remaining > 0) {
            for (int i = 0; i < m && remaining > 0; ++i) {
                // Build the Restricted Candidate List (RCL) for salesman i
                int K = 3;  // RCL size (number of nearest neighbors to consider)
                // Initialize arrays for the K nearest cities
                vector<double> bestDist(K, DBL_MAX);
                vector<int> bestCity(K, -1);
                int cur = currentPos[i];
                // Scan all unvisited cities, find the K nearest
                for (int city = 1; city < n; ++city) {
                    if (!visited[city]) {
                        double dx = coords[cur].first - coords[city].first;
                        double dy = coords[cur].second - coords[city].second;
                        double d2 = dx*dx + dy*dy;  // squared distance
                        // Check if city belongs in the top-K nearest
                        if (d2 < bestDist[K-1]) {
                            // Insert at the appropriate position (keep sorted ascending)
                            int pos = K - 1;
                            while (pos > 0 && d2 < bestDist[pos-1]) {
                                pos--;
                            }
                            for (int t = K - 1; t > pos; --t) {  // shift array elements
                                bestDist[t] = bestDist[t-1];
                                bestCity[t] = bestCity[t-1];
                            }
                            bestDist[pos] = d2;
                            bestCity[pos] = city;
                        }
                    }
                }
                // Collect the candidate list (skip unused slots with -1)
                vector<int> RCL;
                for (int t = 0; t < K; ++t) {
                    if (bestCity[t] != -1) {
                        RCL.push_back(bestCity[t]);
                    }
                }
                if (RCL.empty()) {
                    continue;  // no candidates left (all cities visited)
                }
                // Randomly select one of the nearest cities
                int randIndex = rand() % RCL.size();
                int nextCity = RCL[randIndex];
                // Assign this city to salesman i
                routes[i].push_back(nextCity);
                visited[nextCity] = 1;
                currentPos[i] = nextCity;
                remaining--;
            }
        }

        // *** Phase 2: Local search to improve the constructed solution ***
        // Lambda to compute the total length of a specific route (including depot)
        auto routeDistance = [&](const vector<int>& route) {
            double d = 0.0;
            int prev = 0;  // start from depot (city 0)
            for (int city : route) {
                d += distEuclidean(coords[prev], coords[city]);
                prev = city;
            }
            // return to depot from the last city
            d += distEuclidean(coords[prev], coords[0]);
            return d;
        };

        // 2a. Optimize each route with 2-opt (intra-route)
        for (int k = 0; k < m; ++k) {
            if (routes[k].empty()) continue;  // route is empty (salesman received no cities)
            bool improved = true;
            // Temporarily insert depot at the start and end of the list for easier processing
            vector<int> route = routes[k];
            route.insert(route.begin(), 0);
            route.push_back(0);
            int R = route.size();
            // Standard 2-opt improvement loop
            while (improved) {
                improved = false;
                for (int i = 1; i < R - 2; ++i) {
                    for (int j = i + 1; j < R - 1; ++j) {
                        // Check current and potential distances at the swap boundary
                        double currDist = distEuclidean(coords[route[i-1]], coords[route[i]])
                                        + distEuclidean(coords[route[j]], coords[route[j+1]]);
                        double newDist  = distEuclidean(coords[route[i-1]], coords[route[j]])
                                        + distEuclidean(coords[route[i]], coords[route[j+1]]);
                        if (newDist + 1e-9 < currDist) {
                            // Reverse the city order between i and j
                            reverse(route.begin() + i, route.begin() + j + 1);
                            improved = true;
                        }
                    }
                }
            }
            // Remove the depot nodes added at the start and end
            route.erase(route.begin());
            route.pop_back();
            routes[k] = route;
        }

        // 2b. Inter-route improvement: swap cities between routes
        bool swapped = true;
        while (swapped) {
            swapped = false;
            for (int a = 0; a < m; ++a) {
                for (int b = a + 1; b < m; ++b) {
                    // Consider all pairs of city A from route a and city B from route b
                    for (int i = 0; i < (int)routes[a].size(); ++i) {
                        for (int j = 0; j < (int)routes[b].size(); ++j) {
                            int A = routes[a][i];
                            int B = routes[b][j];
                            // Save the neighbors of A and B in their routes (or depot if at boundary)
                            int prevA = (i == 0 ? 0 : routes[a][i-1]);
                            int nextA = (i == (int)routes[a].size() - 1 ? 0 : routes[a][i+1]);
                            int prevB = (j == 0 ? 0 : routes[b][j-1]);
                            int nextB = (j == (int)routes[b].size() - 1 ? 0 : routes[b][j+1]);
                            // Compute the current combined distances at the positions of A and B
                            double currDist = distEuclidean(coords[prevA], coords[A]) + distEuclidean(coords[A], coords[nextA])
                                            + distEuclidean(coords[prevB], coords[B]) + distEuclidean(coords[B], coords[nextB]);
                            // Compute the distance if A and B are swapped between routes
                            double newDist  = distEuclidean(coords[prevA], coords[B]) + distEuclidean(coords[B], coords[nextA])
                                            + distEuclidean(coords[prevB], coords[A]) + distEuclidean(coords[A], coords[nextB]);
                            if (newDist + 1e-9 < currDist) {
                                // Perform the swap of cities A and B between routes a and b
                                routes[a][i] = B;
                                routes[b][j] = A;
                                // After the swap, apply 2-opt to the affected routes for additional improvement
                                // Route a after the swap:
                                bool imp = true;
                                vector<int> routeA = routes[a];
                                if (!routeA.empty()) {
                                    routeA.insert(routeA.begin(), 0);
                                    routeA.push_back(0);
                                    int Ra = routeA.size();
                                    while (imp) {
                                        imp = false;
                                        for (int p = 1; p < Ra - 2; ++p) {
                                            for (int q = p + 1; q < Ra - 1; ++q) {
                                                double currD = distEuclidean(coords[routeA[p-1]], coords[routeA[p]])
                                                             + distEuclidean(coords[routeA[q]], coords[routeA[q+1]]);
                                                double newD  = distEuclidean(coords[routeA[p-1]], coords[routeA[q]])
                                                             + distEuclidean(coords[routeA[p]], coords[routeA[q+1]]);
                                                if (newD + 1e-9 < currD) {
                                                    reverse(routeA.begin() + p, routeA.begin() + q + 1);
                                                    imp = true;
                                                }
                                            }
                                        }
                                    }
                                    routeA.erase(routeA.begin());
                                    routeA.pop_back();
                                }
                                routes[a] = routeA;
                                // Route b after the swap:
                                imp = true;
                                vector<int> routeB = routes[b];
                                if (!routeB.empty()) {
                                    routeB.insert(routeB.begin(), 0);
                                    routeB.push_back(0);
                                    int Rb = routeB.size();
                                    while (imp) {
                                        imp = false;
                                        for (int p = 1; p < Rb - 2; ++p) {
                                            for (int q = p + 1; q < Rb - 1; ++q) {
                                                double currD = distEuclidean(coords[routeB[p-1]], coords[routeB[p]])
                                                             + distEuclidean(coords[routeB[q]], coords[routeB[q+1]]);
                                                double newD  = distEuclidean(coords[routeB[p-1]], coords[routeB[q]])
                                                             + distEuclidean(coords[routeB[p]], coords[routeB[q+1]]);
                                                if (newD + 1e-9 < currD) {
                                                    reverse(routeB.begin() + p, routeB.begin() + q + 1);
                                                    imp = true;
                                                }
                                            }
                                        }
                                    }
                                    routeB.erase(routeB.begin());
                                    routeB.pop_back();
                                }
                                routes[b] = routeB;
                                swapped = true;
                                goto SWAP_BREAK;  // exit nested loops after a successful swap
                            }
                        }
                    }
                }
            }
            SWAP_BREAK: ;
            // If a swap was performed, the while loop repeats to check for further improvements
        }

        // Compute the total length of the current solution
        double totalDist = 0.0;
        for (int k = 0; k < m; ++k) {
            totalDist += routeDistance(routes[k]);
        }
        // Keep this solution if it is better than the best found so far
        if (totalDist < bestDistance) {
            bestDistance = totalDist;
            bestRoutes = routes;
        }
    }

    // *** Print the best solution found ***
    fout.setf(std::ios::fixed);
    for (int k = 0; k < m; ++k) {
        fout << "Route " << k+1 << ": 0";
        for (int city : bestRoutes[k]) {
            fout << " -> " << city;
        }
        fout << " -> 0\n";
    }
    fout << setprecision(2) << "Total distance: " << bestDistance;
    return 0;
}
