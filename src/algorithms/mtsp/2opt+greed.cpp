#include <bits/stdc++.h>
using namespace std;

int main() {
    ifstream fin("input.txt");
    ofstream fout("output.txt");
    int n, m;
    if (!(fin >> n >> m)) {
        cerr << "Ошибка чтения входного файла\n";
        return 1;
    }
    // Чтение координат городов
    vector<double> x(n), y(n);
    for (int i = 0; i < n; ++i) {
        fin >> x[i] >> y[i];
    }

    // 1. Жадное распределение городов между маршрутами
    vector<vector<int>> routes(m);
    vector<int> currentPos(m, 0);      // текущая позиция каждого коммивояжёра (начинают с депо = 0)
    vector<char> visited(n, 0);
    visited[0] = 1;                    // депо считается посещенным
    int remaining = n - 1;             // количество непосещенных городов (кроме депо)

    // Пока остались нераспределённые города, назначаем их по одному коммивояжёрам
    while (remaining > 0) {
        for (int i = 0; i < m && remaining > 0; ++i) {
            // Поиск ближайшего непосещённого города для коммивояжёра i
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
            if (bestCity == -1) break;  // нет доступных городов (не должно происходить, если remaining > 0)
            // Назначаем найденный ближайший город текущему коммивояжёру
            routes[i].push_back(bestCity);
            visited[bestCity] = 1;
            currentPos[i] = bestCity;
            remaining--;
        }
    }

    // 2. Локальное улучшение: 2-opt оптимизация внутри каждого маршрута
    // Функция расстояния между двумя вершинами (евклидово расстояние)
    auto dist = [&](int a, int b) {
        double dx = x[a] - x[b];
        double dy = y[a] - y[b];
        return sqrt(dx*dx + dy*dy);
    };

    // Применяем 2-opt для каждого маршрута
    for (int k = 0; k < m; ++k) {
        // Подготавливаем вспомогательный список с включением депо в начало и конец маршрута
        vector<int> route = routes[k];
        route.insert(route.begin(), 0);  // депо в начало
        route.push_back(0);             // депо в конец
        bool improved = true;
        int routeSize = route.size();
        // Итеративно пытаемся улучшить маршрут, выполняя обмен 2-opt
        while (improved) {
            improved = false;
            // Проверяем все пары рёбер (i-1,i) и (j,j+1) в маршруте
            for (int i = 1; i < routeSize - 2; ++i) {
                for (int j = i + 1; j < routeSize - 1; ++j) {
                    // Длины старых рёбер
                    double dist_before = dist(route[i-1], route[i]) + dist(route[j], route[j+1]);
                    // Длины новых рёбер, если выполнить 2-opt обмен (разворот сегмента [i, j])
                    double dist_after  = dist(route[i-1], route[j]) + dist(route[i], route[j+1]);
                    if (dist_after + 1e-9 < dist_before) {
                        // Улучшение найдено: переворачиваем порядок городов между i и j включительно
                        reverse(route.begin() + i, route.begin() + j + 1);
                        improved = true;
                    }
                }
            }
        }
        // Убираем добавленные в начале/конце депо, сохраняем улучшенную последовательность
        route.erase(route.begin());         // убрать начальный 0
        route.pop_back();                   // убрать конечный 0
        routes[k] = route;
    }

    // 3. Вычисление итоговых расстояний и вывод
    double totalDistance = 0.0;
    fout.setf(std::ios::fixed);
    for (int k = 0; k < m; ++k) {
        // Вывод маршрута k (с депо в начале и конце)
        fout << "Route " << k+1 << ": 0";
        for (int city : routes[k]) {
            fout << " -> " << city;
        }
        fout << " -> 0\n";
        // Вычисляем длину маршрута k
        double routeDist = 0.0;
        int prev = 0;
        for (int city : routes[k]) {
            routeDist += dist(prev, city);
            prev = city;
        }
        routeDist += dist(prev, 0);  // возвращаемся в депо
        totalDistance += routeDist;
    }
    fout << setprecision(2) << "Total distance: " << totalDistance;
    return 0;
}
