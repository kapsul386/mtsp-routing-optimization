#!/usr/bin/env bash

python experiments/generate_mtsp_instances.py
python experiments/run_benchmarks.py
python experiments/build_report.py
