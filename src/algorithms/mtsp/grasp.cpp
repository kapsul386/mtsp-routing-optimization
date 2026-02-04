#include <bits/stdc++.h>
using namespace std;

// Вычисление евклидова расстояния между двумя точками (городами)
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
    // Считываем координаты городов (0-й город - депо)
    vector<pair<double,double>> coords(n);
    for (int i = 0; i < n; ++i) {
        fin >> coords[i].first >> coords[i].second;
    }
    srand(time(NULL));  // инициализируем ГСЧ

    double bestDistance = DBL_MAX;
    vector<vector<int>> bestRoutes;  // сохранение лучшего решения

    int maxIterations = 50;  // число итераций GRASP (можно настроить)
    for (int iter = 0; iter < maxIterations; ++iter) {
        // *** Фаза 1: Построение случайно-жадного начального решения ***
        vector<char> visited(n, 0);
        visited[0] = 1;                    // депо посещено всеми
        int remaining = n - 1;
        vector<vector<int>> routes(m);
        vector<int> currentPos(m, 0);      // текущая позиция каждого коммивояжёра (индекс города)

        // Распределяем все города между маршрутами
        while (remaining > 0) {
            for (int i = 0; i < m && remaining > 0; ++i) {
                // Формируем список кандидатов (RCL) для коммивояжёра i
                int K = 3;  // размер RCL (сколько ближайших рассматривать)
                // Инициализируем массивы для ближайших K городов
                vector<double> bestDist(K, DBL_MAX);
                vector<int> bestCity(K, -1);
                int cur = currentPos[i];
                // Проходим по всем непосещённым городам, ищем K ближайших
                for (int city = 1; city < n; ++city) {
                    if (!visited[city]) {
                        double dx = coords[cur].first - coords[city].first;
                        double dy = coords[cur].second - coords[city].second;
                        double d2 = dx*dx + dy*dy;  // расстояние в квадрате
                        // Проверяем, попадает ли city в топ-K ближайших
                        if (d2 < bestDist[K-1]) {
                            // Вставляем на подходящую позицию (упорядочиваем по возрастанию)
                            int pos = K - 1;
                            while (pos > 0 && d2 < bestDist[pos-1]) {
                                pos--;
                            }
                            for (int t = K - 1; t > pos; --t) {  // сдвиг элементов массива
                                bestDist[t] = bestDist[t-1];
                                bestCity[t] = bestCity[t-1];
                            }
                            bestDist[pos] = d2;
                            bestCity[pos] = city;
                        }
                    }
                }
                // Собираем список кандидатов (игнорируем неиспользованные слоты -1)
                vector<int> RCL;
                for (int t = 0; t < K; ++t) {
                    if (bestCity[t] != -1) {
                        RCL.push_back(bestCity[t]);
                    }
                }
                if (RCL.empty()) {
                    continue;  // не осталось кандидатов (все города посещены)
                }
                // Случайно выбираем один из ближайших городов
                int randIndex = rand() % RCL.size();
                int nextCity = RCL[randIndex];
                // Назначаем этот город коммивояжёру i
                routes[i].push_back(nextCity);
                visited[nextCity] = 1;
                currentPos[i] = nextCity;
                remaining--;
            }
        }

        // *** Фаза 2: Локальный поиск для улучшения построенного решения ***
        // Определяем функцию расчета полной длины конкретного маршрута (с депо)
        auto routeDistance = [&](const vector<int>& route) {
            double d = 0.0;
            int prev = 0;  // начинаем из депо (город 0)
            for (int city : route) {
                d += distEuclidean(coords[prev], coords[city]);
                prev = city;
            }
            // возвращаемся в депо из последнего города
            d += distEuclidean(coords[prev], coords[0]);
            return d;
        };

        // 2a. Оптимизация каждого маршрута по 2-opt (внутри маршрута)
        for (int k = 0; k < m; ++k) {
            if (routes[k].empty()) continue;  // маршрут пуст (коммивояжёр не получил городов)
            bool improved = true;
            // Добавляем фиктивно депо в начало и конец списка для удобства обработки
            vector<int> route = routes[k];
            route.insert(route.begin(), 0);
            route.push_back(0);
            int R = route.size();
            // Стандартный цикл 2-opt улучшения
            while (improved) {
                improved = false;
                for (int i = 1; i < R - 2; ++i) {
                    for (int j = i + 1; j < R - 1; ++j) {
                        // Проверяем текущее и потенциальное расстояние на границах разрыва
                        double currDist = distEuclidean(coords[route[i-1]], coords[route[i]]) 
                                        + distEuclidean(coords[route[j]], coords[route[j+1]]);
                        double newDist  = distEuclidean(coords[route[i-1]], coords[route[j]]) 
                                        + distEuclidean(coords[route[i]], coords[route[j+1]]);
                        if (newDist + 1e-9 < currDist) {
                            // Разворачиваем порядок городов между i и j
                            reverse(route.begin() + i, route.begin() + j + 1);
                            improved = true;
                        }
                    }
                }
            }
            // Убираем добавленные депо из начала и конца
            route.erase(route.begin());
            route.pop_back();
            routes[k] = route;
        }

        // 2b. Межмаршрутные улучшения: обмен городов между маршрутами
        bool swapped = true;
        while (swapped) {
            swapped = false;
            for (int a = 0; a < m; ++a) {
                for (int b = a + 1; b < m; ++b) {
                    // Рассматриваем все пары городов A из маршрута a и B из маршрута b
                    for (int i = 0; i < (int)routes[a].size(); ++i) {
                        for (int j = 0; j < (int)routes[b].size(); ++j) {
                            int A = routes[a][i];
                            int B = routes[b][j];
                            // Сохраняем соседей A и B в маршрутах (или депо, если в конце/начале)
                            int prevA = (i == 0 ? 0 : routes[a][i-1]);
                            int nextA = (i == (int)routes[a].size() - 1 ? 0 : routes[a][i+1]);
                            int prevB = (j == 0 ? 0 : routes[b][j-1]);
                            int nextB = (j == (int)routes[b].size() - 1 ? 0 : routes[b][j+1]);
                            // Вычисляем текущие суммарные расстояния на месте A и B
                            double currDist = distEuclidean(coords[prevA], coords[A]) + distEuclidean(coords[A], coords[nextA])
                                            + distEuclidean(coords[prevB], coords[B]) + distEuclidean(coords[B], coords[nextB]);
                            // Вычисляем расстояние, если поменять A и B местами между маршрутами
                            double newDist  = distEuclidean(coords[prevA], coords[B]) + distEuclidean(coords[B], coords[nextA])
                                            + distEuclidean(coords[prevB], coords[A]) + distEuclidean(coords[A], coords[nextB]);
                            if (newDist + 1e-9 < currDist) {
                                // Выполняем обмен городов A и B между маршрутами a и b
                                routes[a][i] = B;
                                routes[b][j] = A;
                                // После обмена применяем 2-opt к затронутым маршрутам для дополнительного улучшения
                                // Маршрут a после обмена:
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
                                // Маршрут b после обмена:
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
                                goto SWAP_BREAK;  // выходим из вложенных циклов после успешного обмена
                            }
                        }
                    }
                }
            }
            SWAP_BREAK: ;
            // Если был выполнен обмен, цикл while повторяется, чтобы учесть новые возможности улучшения
        }

        // Вычисляем суммарную длину полученного решения
        double totalDist = 0.0;
        for (int k = 0; k < m; ++k) {
            totalDist += routeDistance(routes[k]);
        }
        // Сохраняем решение, если оно лучше найденного ранее
        if (totalDist < bestDistance) {
            bestDistance = totalDist;
            bestRoutes = routes;
        }
    }

    // *** Вывод лучшего найденного решения ***
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
