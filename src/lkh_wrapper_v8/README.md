# lkh-wrapper-v8 layout

`mtsp_lkh_wrapper_v8.cpp` is the entry point compiled by CMake. The files in this
folder are included from that entry point in order, so `--step lkh-wrapper-v8`
continues to work without changing run commands.

- `00_common.cpp` - shared budget, distance, route index, grid, and small utility helpers.
- `10_candidate_sets.cpp` - geometric candidates and POPMUSIC/hybrid candidate enrichment.
- `20_route_local_search.cpp` - construction fallback, 2-opt cleanup, kicks, ILS, and assignment completion.
- `30_cluster_model.cpp` - lightweight clustering and cluster bridge candidates.
- `40_seed_routes.cpp` - cluster ordering, fast seed routes, route sanitizing, and cluster-aware initial routes.
- `50_rebalance.cpp` - cluster block rebalancing and inter-route relocation helpers.
- `60_solver.cpp` - solver configuration, orchestration pipeline, and `lkh-wrapper-v8` registration.

When adding the next improvement, put the local implementation in the closest
module above and keep the orchestration decision in `60_solver.cpp`.
