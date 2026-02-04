#include <bits/stdc++.h>
using namespace std;

int main() {
    // Открываем файлы ввода и вывода
    ifstream fin("input.txt");
    ofstream fout("output.txt");
    int n, m;
    if (!(fin >> n >> m)) {
        cerr << "Ошибка чтения входных данных\n";
        return 1;
    }
    // Читаем координаты городов (0-й город - депо)
    vector<double> x(n), y(n);
    for (int i = 0; i < n; ++i) {
        fin >> x[i] >> y[i];
    }

    // 1. Случайное распределение городов между коммивояжёрами
    vector<int> cities;
    for (int city = 1; city < n; ++city) {
        cities.push_back(city);
    }
    srand(time(NULL));                  // инициализируем генератор случайностей
    random_shuffle(cities.begin(), cities.end());  // случайно перемешиваем список городов

    // Разбиваем список городов на M маршрутов (примерно равномерно)
    vector<vector<int>> routes(m);
    for (size_t idx = 0; idx < cities.size(); ++idx) {
        int salesman = idx % m;
        routes[salesman].push_back(cities[idx]);
    }

    // 2. Построение маршрута для каждого коммивояжёра по принципу ближайшего соседа
    for (int k = 0; k < m; ++k) {
        int current = 0;                      // текущая позиция (начинаем с депо = 0)
        vector<int> orderedRoute;             // новый упорядоченный маршрут
        vector<bool> used(routes[k].size());  // отметки посещённых городов в маршруте

        // Пока остаются неупорядоченные города в списке данного коммивояжёра
        for (int count = routes[k].size(); count > 0; --count) {
            // Находим ближайший неиспользованный город от текущей позиции
            double bestDist = DBL_MAX;
            int bestIndex = -1;
            for (int j = 0; j < (int)routes[k].size(); ++j) {
                if (!used[j]) {
                    int city = routes[k][j];
                    double dx = x[current] - x[city];
                    double dy = y[current] - y[city];
                    double distSq = dx*dx + dy*dy;  // расстояние в квадрате (для сравнения)
                    if (distSq < bestDist) {
                        bestDist = distSq;
                        bestIndex = j;
                    }
                }
            }
            // Добавляем найденный город в маршрут
            used[bestIndex] = true;
            current = routes[k][bestIndex];
            orderedRoute.push_back(current);
        }
        // Сохраняем упорядоченный маршрут вместо исходного беспорядочного
        routes[k] = orderedRoute;
    }

    // 3. Вычисление расстояний и формирование вывода
    double totalDistance = 0.0;
    fout.setf(std::ios::fixed);
    // Выводим маршруты каждого коммивояжёра
    for (int k = 0; k < m; ++k) {
        // Вывод маршрута: начинаем с депо (0), затем последовательность городов, и обратно в 0
        fout << "Route " << k+1 << ": 0";
        for (int city : routes[k]) {
            fout << " -> " << city;
        }
        fout << " -> 0\n";
        // Вычисляем длину маршрута k
        double routeDist = 0.0;
        int prev = 0;  // стартуем из депо
        for (int city : routes[k]) {
            double dx = x[prev] - x[city];
            double dy = y[prev] - y[city];
            routeDist += sqrt(dx*dx + dy*dy);
            prev = city;
        }
        // добавляем расстояние от последнего города обратно до депо
        double dx = x[prev] - x[0];
        double dy = y[prev] - y[0];
        routeDist += sqrt(dx*dx + dy*dy);
        totalDistance += routeDist;
    }
    // Выводим суммарную длину всех маршрутов (с двумя знаками после запятой)
    fout << setprecision(2) << "Total distance: " << totalDistance;
    return 0;
}
