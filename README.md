# mtsp-routing-optimization
Учебный проект посвящён исследованию и реализации эффективных эвристических методов оптимизации маршрутов в задаче множественного коммивояжёра (mTSP). Основная цель — анализ и сравнение различных подходов к решению mTSP с точки зрения качества получаемых маршрутов и масштабируемости на задачах большой размерности.

# Optimization of Routing Algorithms for mTSP

This project explores heuristic optimization methods for the Multiple Traveling Salesman Problem (mTSP), with a focus on Lin–Kernighan–Helsgaun (LKH) algorithm adaptation and scalability analysis.

##  Goal
To analyze and implement scalable heuristic algorithms for mTSP and evaluate their performance on large-scale instances.

##  Project Structure
- `src/` – source code and LKH integration
- `data/` – problem instances (TSPLIB + synthetic)
- `docs/` – research documentation (reports, plans)
- `experiments/` – experiment configs and results

##  Methods
- Greedy heuristics
- 2-opt, k-opt
- LKH algorithm (external C library)
- Experimental comparison on TSPLIB-derived datasets

##  Requirements
- C++ compiler
- CMake
- Python (for plotting results)

## Run
```bash
cd experiments
bash run.sh
