# Hybrid Baseline Report

This report compares the selected mTSP solvers against the best successful baseline per instance. The baseline pool is tiered by `n`: exact MIP for small cases, OR-Tools Routing for medium cases, and TSP-transform + LKH for large cases.

## Aggregated Tables

### uniform | n=20, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 47.054747 | 41.100265 | 14.487697 | 0.000468 / 5.000185 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 47.054747 | 41.100265 | 14.487697 | 0.000392 / 5.000185 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 57.563579 | 41.100265 | 40.056467 | 2.3e-05 / 5.000185 | ortools-gls x1 | 0 |
| rand+nn | 1 | 71.766424 | 41.100265 | 74.613044 | 2.1e-05 / 5.000185 | ortools-gls x1 | 0 |

### uniform | n=20, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 53.930383 | 44.86439 | 20.207548 | 0.000368 / 5.00047 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 53.930383 | 44.86439 | 20.207548 | 0.000333 / 5.00047 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 62.572758 | 44.86439 | 39.470877 | 2.2e-05 / 5.00047 | ortools-gls x1 | 0 |
| rand+nn | 1 | 89.467867 | 44.86439 | 99.418441 | 3.1e-05 / 5.00047 | ortools-gls x1 | 0 |

### uniform | n=20, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 44.693508 | 36.828012 | 21.357373 | 0.000789 / 5.000568 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 50.822059 | 36.828012 | 37.998377 | 0.000964 / 5.000568 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 92.317096 | 36.828012 | 150.670864 | 2.5e-05 / 5.000568 | ortools-gls x1 | 0 |
| rand+nn | 1 | 101.679573 | 36.828012 | 176.093027 | 2.3e-05 / 5.000568 | ortools-gls x1 | 0 |

### uniform | n=50, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 167.677795 | 150.011739 | 11.776449 | 0.00552 / 0.042165 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 171.612879 | 150.011739 | 14.399633 | 0.004478 / 0.042165 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 181.328248 | 150.011739 | 20.876039 | 5.9e-05 / 0.042165 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 224.914817 | 150.011739 | 49.931478 | 3.1e-05 / 0.042165 | tsp-transform-lkh x1 | 0 |

### uniform | n=50, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 160.114651 | 144.432357 | 10.857881 | 0.002054 / 5.000656 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 163.085561 | 144.432357 | 12.914837 | 0.001883 / 5.000656 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 198.260312 | 144.432357 | 37.268626 | 5.7e-05 / 5.000656 | ortools-gls x1 | 0 |
| rand+nn | 1 | 288.290248 | 144.432357 | 99.602259 | 2.8e-05 / 5.000656 | ortools-gls x1 | 0 |

### uniform | n=50, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 186.397887 | 154.076264 | 20.977678 | 0.001576 / 5.000759 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 188.548199 | 154.076264 | 22.373294 | 0.002143 / 5.000759 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 242.845041 | 154.076264 | 57.613532 | 4.9e-05 / 5.000759 | ortools-gls x1 | 0 |
| rand+nn | 1 | 411.77599 | 154.076264 | 167.254656 | 4e-05 / 5.000759 | ortools-gls x1 | 0 |

### uniform | n=100, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 433.728831 | 415.025483 | 4.506554 | 0.005284 / 5.000119 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 436.410467 | 415.025483 | 5.152692 | 0.004055 / 5.000119 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 461.316171 | 415.025483 | 11.153698 | 0.000224 / 5.000119 | ortools-gls x1 | 0 |
| rand+nn | 1 | 763.49026 | 415.025483 | 83.96226 | 4.6e-05 / 5.000119 | ortools-gls x1 | 0 |

### uniform | n=100, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 454.347437 | 412.801739 | 10.064322 | 0.011218 / 0.075832 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 454.38416 | 412.801739 | 10.073218 | 0.015612 / 0.075832 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 637.016949 | 412.801739 | 54.315471 | 0.0002 / 0.075832 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 894.021277 | 412.801739 | 116.574009 | 4.6e-05 / 0.075832 | tsp-transform-lkh x1 | 0 |

### uniform | n=100, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 477.62378 | 424.430883 | 12.532758 | 0.00513 / 5.000842 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 488.94322 | 424.430883 | 15.199727 | 0.006246 / 5.000842 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 812.708304 | 424.430883 | 91.481896 | 0.000126 / 5.000842 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1055.698335 | 424.430883 | 148.732686 | 6.3e-05 / 5.000842 | ortools-gls x1 | 0 |

### uniform | n=200, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 1283.221982 | 1120.704602 | 14.501357 | 0.029696 / 0.165137 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 1293.741711 | 1120.704602 | 15.440028 | 0.021152 / 0.165137 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 1333.114546 | 1120.704602 | 18.95325 | 0.000499 / 0.165137 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 2076.765008 | 1120.704602 | 85.308868 | 8.8e-05 / 0.165137 | tsp-transform-lkh x1 | 0 |

### uniform | n=200, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 1189.184776 | 1101.87953 | 7.923302 | 0.08259 / 0.23231 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 1203.612325 | 1101.87953 | 9.23266 | 0.030017 / 0.23231 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 1632.943413 | 1101.87953 | 48.196184 | 0.000456 / 0.23231 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 2317.235039 | 1101.87953 | 110.298402 | 9.7e-05 / 0.23231 | tsp-transform-lkh x1 | 0 |

### uniform | n=200, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 1258.346789 | 1129.269177 | 11.43019 | 0.070779 / 5.00154 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 1314.752715 | 1129.269177 | 16.425095 | 0.034511 / 5.00154 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 1706.977859 | 1129.269177 | 51.157748 | 0.000539 / 5.00154 | ortools-gls x1 | 0 |
| rand+nn | 1 | 3050.936115 | 1129.269177 | 170.169077 | 7.1e-05 / 5.00154 | ortools-gls x1 | 0 |

### uniform | n=500, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 4572.740986 | 4443.631037 | 2.905506 | 0.503923 / 0.253451 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 4629.786229 | 4443.631037 | 4.189259 | 0.220217 / 0.253451 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 5014.809464 | 4443.631037 | 12.853867 | 0.003464 / 0.253451 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 7099.979007 | 4443.631037 | 59.77877 | 0.000317 / 0.253451 | tsp-transform-lkh x1 | 0 |

### uniform | n=500, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 4498.787235 | 4401.692443 | 2.205851 | 1.237134 / 0.276707 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 4550.936941 | 4401.692443 | 3.390616 | 0.362213 / 0.276707 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 5058.195874 | 4401.692443 | 14.914796 | 0.002905 / 0.276707 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 9301.20131 | 4401.692443 | 111.309659 | 0.000276 / 0.276707 | tsp-transform-lkh x1 | 0 |

### uniform | n=500, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 4912.278434 | 4432.640973 | 10.82058 | 0.205368 / 0.283029 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 5228.32885 | 4432.640973 | 17.95065 | 1.079617 / 0.283029 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 6591.231722 | 4432.640973 | 48.697622 | 0.003085 / 0.283029 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 12119.536601 | 4432.640973 | 173.415706 | 0.000216 / 0.283029 | tsp-transform-lkh x1 | 0 |

### uniform | n=1000, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 13330.443242 | 12596.260075 | 5.828581 | 5.741278 / 1.125554 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 13339.959306 | 12596.260075 | 5.904127 | 12.385038 / 1.125554 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 14820.126694 | 12596.260075 | 17.654975 | 0.017838 / 1.125554 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 25437.119473 | 12596.260075 | 101.941841 | 0.00096 / 1.125554 | tsp-transform-lkh x1 | 0 |

### uniform | n=1000, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 13635.505502 | 12349.028878 | 10.417634 | 2.77007 / 1.063057 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 13996.68999 | 12349.028878 | 13.342435 | 15.736722 / 1.063057 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 16881.455644 | 12349.028878 | 36.702698 | 0.015493 / 1.063057 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 33892.313895 | 12349.028878 | 174.453273 | 0.00137 / 1.063057 | tsp-transform-lkh x1 | 0 |

### uniform | n=1000, m=7

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 14233.511858 | 12424.382636 | 14.56112 | 13.720269 / 1.275093 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 14468.404971 | 12424.382636 | 16.451701 | 3.624874 / 1.275093 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 16930.088828 | 12424.382636 | 36.265031 | 0.010431 / 1.275093 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 39427.926052 | 12424.382636 | 217.343141 | 0.00083 / 1.275093 | tsp-transform-lkh x1 | 0 |

### clustered-center | n=20, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 49.758306 | 44.745373 | 11.203243 | 0.000442 / 5.000035 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 49.758306 | 44.745373 | 11.203243 | 0.000351 / 5.000035 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 63.727055 | 44.745373 | 42.421553 | 3.9e-05 / 5.000035 | ortools-gls x1 | 0 |
| rand+nn | 1 | 80.45529 | 44.745373 | 79.806949 | 1.9e-05 / 5.000035 | ortools-gls x1 | 0 |

### clustered-center | n=20, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 53.520778 | 43.877905 | 21.976603 | 0.000351 / 5.001516 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 53.520778 | 43.877905 | 21.976603 | 0.000296 / 5.001516 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 88.54031 | 43.877905 | 101.787916 | 2.2e-05 / 5.001516 | ortools-gls x1 | 0 |
| rand+nn | 1 | 112.439588 | 43.877905 | 156.255598 | 3.2e-05 / 5.001516 | ortools-gls x1 | 0 |

### clustered-center | n=20, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 44.659719 | 38.744569 | 15.267043 | 0.000615 / 5.000544 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 44.659719 | 38.744569 | 15.267043 | 0.000457 / 5.000544 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 99.921589 | 38.744569 | 157.898311 | 2.5e-05 / 5.000544 | ortools-gls x1 | 0 |
| rand+nn | 1 | 127.404112 | 38.744569 | 228.830892 | 2.3e-05 / 5.000544 | ortools-gls x1 | 0 |

### clustered-center | n=50, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 143.474983 | 122.765884 | 16.868774 | 0.003458 / 4.999964 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 143.795147 | 122.765884 | 17.129566 | 0.00154 / 4.999964 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 175.79265 | 122.765884 | 43.193405 | 5.6e-05 / 4.999964 | ortools-gls x1 | 0 |
| rand+nn | 1 | 215.38539 | 122.765884 | 75.44401 | 3e-05 / 4.999964 | ortools-gls x1 | 0 |

### clustered-center | n=50, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 148.213061 | 126.387072 | 17.269163 | 0.002706 / 4.999979 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 148.338563 | 126.387072 | 17.368462 | 0.002659 / 4.999979 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 203.042191 | 126.387072 | 60.651076 | 6e-05 / 4.999979 | ortools-gls x1 | 0 |
| rand+nn | 1 | 291.151934 | 126.387072 | 130.365281 | 3.8e-05 / 4.999979 | ortools-gls x1 | 0 |

### clustered-center | n=50, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 147.697518 | 115.376314 | 28.013726 | 0.002839 / 5.000338 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 148.261137 | 115.376314 | 28.502231 | 0.001566 / 5.000338 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 316.198869 | 115.376314 | 174.058737 | 5.2e-05 / 5.000338 | ortools-gls x1 | 0 |
| rand+nn | 1 | 431.681531 | 115.376314 | 274.150912 | 3.1e-05 / 5.000338 | ortools-gls x1 | 0 |

### clustered-center | n=100, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 241.103859 | 235.176596 | 2.520346 | 0.00514 / 5.000621 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 241.659906 | 235.176596 | 2.756784 | 0.004741 / 5.000621 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 354.975624 | 235.176596 | 50.94003 | 0.000202 / 5.000621 | ortools-gls x1 | 0 |
| rand+nn | 1 | 391.839938 | 235.176596 | 66.615192 | 4.5e-05 / 5.000621 | ortools-gls x1 | 0 |

### clustered-center | n=100, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 266.967263 | 248.291092 | 7.521885 | 0.003934 / 5.000799 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 268.337291 | 248.291092 | 8.073668 | 0.005648 / 5.000799 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 413.114649 | 248.291092 | 66.383194 | 0.000226 / 5.000799 | ortools-gls x1 | 0 |
| rand+nn | 1 | 603.526857 | 248.291092 | 143.072296 | 4.3e-05 / 5.000799 | ortools-gls x1 | 0 |

### clustered-center | n=100, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 324.746453 | 249.114022 | 30.360568 | 0.010718 / 5.000539 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 347.844827 | 249.114022 | 39.632777 | 0.006681 / 5.000539 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 552.750168 | 249.114022 | 121.886413 | 0.000124 / 5.000539 | ortools-gls x1 | 0 |
| rand+nn | 1 | 856.641308 | 249.114022 | 243.875187 | 4.5e-05 / 5.000539 | ortools-gls x1 | 0 |

### clustered-center | n=200, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 299.761805 | 296.181468 | 1.208832 | 0.015127 / 5.000852 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 303.402859 | 296.181468 | 2.438164 | 0.020078 / 5.000852 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 368.3072 | 296.181468 | 24.351872 | 0.000658 / 5.000852 | ortools-gls x1 | 0 |
| rand+nn | 1 | 536.233986 | 296.181468 | 81.049135 | 8.9e-05 / 5.000852 | ortools-gls x1 | 0 |

### clustered-center | n=200, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 311.849175 | 305.354597 | 2.126897 | 0.013452 / 5.000644 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 312.047035 | 305.354597 | 2.191694 | 0.025076 / 5.000644 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 549.078739 | 305.354597 | 79.816759 | 0.000387 / 5.000644 | ortools-gls x1 | 0 |
| rand+nn | 1 | 735.854341 | 305.354597 | 140.983548 | 7.8e-05 / 5.000644 | ortools-gls x1 | 0 |

### clustered-center | n=200, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 373.939293 | 335.034743 | 11.612094 | 0.028757 / 5.000539 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 375.489491 | 335.034743 | 12.074792 | 0.013496 / 5.000539 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 651.492507 | 335.034743 | 94.455208 | 0.000492 / 5.000539 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1068.894049 | 335.034743 | 219.039763 | 7.8e-05 / 5.000539 | ortools-gls x1 | 0 |

### clustered-center | n=500, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 1033.312334 | 1041.766363 | -0.811509 | 0.248687 / 5.001087 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 1033.756885 | 1041.766363 | -0.768836 | 0.118118 / 5.001087 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 1351.981451 | 1041.766363 | 29.777798 | 0.00417 / 5.001087 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1881.11239 | 1041.766363 | 80.569507 | 0.000324 / 5.001087 | ortools-gls x1 | 0 |

### clustered-center | n=500, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 1040.916 | 1080.086405 | -3.626599 | 0.338444 / 5.000934 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 1057.479193 | 1080.086405 | -2.093093 | 0.066585 / 5.000934 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 1603.110348 | 1080.086405 | 48.424269 | 0.00211 / 5.000934 | ortools-gls x1 | 0 |
| rand+nn | 1 | 2355.189563 | 1080.086405 | 118.055662 | 0.000257 / 5.000934 | ortools-gls x1 | 0 |

### clustered-center | n=500, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 1266.238493 | 1205.83123 | 5.009595 | 0.054747 / 5.000525 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 1273.103344 | 1205.83123 | 5.5789 | 0.381747 / 5.000525 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 2095.265347 | 1205.83123 | 73.761078 | 0.00199 / 5.000525 | ortools-gls x1 | 0 |
| rand+nn | 1 | 3608.929737 | 1205.83123 | 199.289788 | 0.000322 / 5.000525 | ortools-gls x1 | 0 |

### clustered-center | n=1000, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 3214.801986 | 3335.625776 | -3.622223 | 2.544988 / 1.226521 | tsp-transform-lkh x1 | 1 |
| lkh-wrapper-v3 | 1 | 3227.650724 | 3335.625776 | -3.237025 | 0.739601 / 1.226521 | tsp-transform-lkh x1 | 1 |
| 2opt+greed | 1 | 3955.030828 | 3335.625776 | 18.569381 | 0.015367 / 1.226521 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 6799.669318 | 3335.625776 | 103.849885 | 0.001523 / 1.226521 | tsp-transform-lkh x1 | 0 |

### clustered-center | n=1000, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 3307.827614 | 3472.404292 | -4.73956 | 3.068282 / 5.00174 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 3314.017393 | 3472.404292 | -4.561304 | 0.234575 / 5.00174 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 5026.595117 | 3472.404292 | 44.758349 | 0.012 / 5.00174 | ortools-gls x1 | 0 |
| rand+nn | 1 | 9511.20608 | 3472.404292 | 173.908372 | 0.002661 / 5.00174 | ortools-gls x1 | 0 |

### clustered-center | n=1000, m=7

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 3318.582621 | 3398.734214 | -2.358278 | 0.251277 / 5.0025 | ortools-gls x1 | 1 |
| lkh-wrapper-v2 | 1 | 3503.177318 | 3398.734214 | 3.073 | 10.641077 / 5.0025 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 5755.324768 | 3398.734214 | 69.3373 | 0.007238 / 5.0025 | ortools-gls x1 | 0 |
| rand+nn | 1 | 11775.341896 | 3398.734214 | 246.46257 | 0.000866 / 5.0025 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=20, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 54.536331 | 40.038831 | 36.2086 | 0.000552 / 5.000404 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 54.536331 | 40.038831 | 36.2086 | 0.00053 / 5.000404 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 68.396762 | 40.038831 | 70.826071 | 2.3e-05 / 5.000404 | ortools-gls x1 | 0 |
| rand+nn | 1 | 69.894847 | 40.038831 | 74.567652 | 3.7e-05 / 5.000404 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=20, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 47.401893 | 36.814266 | 28.759577 | 0.000618 / 5.000442 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 47.401893 | 36.814266 | 28.759577 | 0.000435 / 5.000442 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 78.525284 | 36.814266 | 113.301235 | 2.8e-05 / 5.000442 | ortools-gls x1 | 0 |
| rand+nn | 1 | 88.895757 | 36.814266 | 141.470948 | 2.2e-05 / 5.000442 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=20, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 67.804122 | 47.901417 | 41.549303 | 0.000689 / 5.000288 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 67.804122 | 47.901417 | 41.549303 | 0.000642 / 5.000288 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 135.049296 | 47.901417 | 181.931735 | 2.5e-05 / 5.000288 | ortools-gls x1 | 0 |
| rand+nn | 1 | 143.944879 | 47.901417 | 200.50234 | 2.2e-05 / 5.000288 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=50, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 70.473353 | 65.386054 | 7.780404 | 0.002603 / 5.000833 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 72.858662 | 65.386054 | 11.428443 | 0.001597 / 5.000833 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 91.392755 | 65.386054 | 39.774079 | 8.9e-05 / 5.000833 | ortools-gls x1 | 0 |
| rand+nn | 1 | 112.124464 | 65.386054 | 71.480701 | 3.7e-05 / 5.000833 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=50, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 110.192674 | 89.469417 | 23.162392 | 0.002236 / 5.000832 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 110.192674 | 89.469417 | 23.162392 | 0.001244 / 5.000832 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 132.879663 | 89.469417 | 48.519648 | 5.2e-05 / 5.000832 | ortools-gls x1 | 0 |
| rand+nn | 1 | 203.952582 | 89.469417 | 127.957875 | 2.8e-05 / 5.000832 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=50, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 124.315956 | 102.201281 | 21.638354 | 0.002011 / 5.000835 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 124.315956 | 102.201281 | 21.638354 | 0.001382 / 5.000835 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 231.5311 | 102.201281 | 126.544225 | 5.1e-05 / 5.000835 | ortools-gls x1 | 0 |
| rand+nn | 1 | 308.504442 | 102.201281 | 201.859663 | 3.1e-05 / 5.000835 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=100, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 185.514298 | 183.710468 | 0.981887 | 0.007112 / 5.000787 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 188.970161 | 183.710468 | 2.863034 | 0.004102 / 5.000787 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 287.327772 | 183.710468 | 56.402504 | 0.000202 / 5.000787 | ortools-gls x1 | 0 |
| rand+nn | 1 | 353.506765 | 183.710468 | 92.426033 | 4.5e-05 / 5.000787 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=100, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 190.249456 | 194.354246 | -2.112015 | 0.007246 / 5.00082 | ortools-gls x1 | 1 |
| lkh-wrapper-v2 | 1 | 193.447099 | 194.354246 | -0.466749 | 0.005281 / 5.00082 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 350.072441 | 194.354246 | 80.120809 | 0.000127 / 5.00082 | ortools-gls x1 | 0 |
| rand+nn | 1 | 449.526081 | 194.354246 | 131.292133 | 4.9e-05 / 5.00082 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=100, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 145.961164 | 150.352625 | -2.920774 | 0.005732 / 5.000217 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 180.76145 | 150.352625 | 20.225004 | 0.039142 / 5.000217 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 364.604012 | 150.352625 | 142.499266 | 0.000111 / 5.000217 | ortools-gls x1 | 0 |
| rand+nn | 1 | 452.756628 | 150.352625 | 201.129846 | 4.9e-05 / 5.000217 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=200, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 305.761912 | 309.986524 | -1.362837 | 0.022903 / 5.000146 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 311.201017 | 309.986524 | 0.391789 | 0.053899 / 5.000146 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 469.473329 | 309.986524 | 51.449593 | 0.000769 / 5.000146 | ortools-gls x1 | 0 |
| rand+nn | 1 | 585.959619 | 309.986524 | 89.027449 | 8.7e-05 / 5.000146 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=200, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 306.48582 | 314.771184 | -2.632186 | 0.022002 / 5.000628 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 316.387901 | 314.771184 | 0.513617 | 0.050406 / 5.000628 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 633.826392 | 314.771184 | 101.360996 | 0.000463 / 5.000628 | ortools-gls x1 | 0 |
| rand+nn | 1 | 766.644397 | 314.771184 | 143.556093 | 7.5e-05 / 5.000628 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=200, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 309.780158 | 316.136383 | -2.010596 | 0.026376 / 5.001262 | ortools-gls x1 | 1 |
| lkh-wrapper-v2 | 1 | 311.223046 | 316.136383 | -1.554183 | 0.017036 / 5.001262 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 860.988793 | 316.136383 | 172.347265 | 0.000332 / 5.001262 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1071.04916 | 316.136383 | 238.793387 | 7.4e-05 / 5.001262 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=500, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 1086.145795 | 1148.033981 | -5.390797 | 0.318206 / 5.00156 | ortools-gls x1 | 1 |
| lkh-wrapper-v2 | 1 | 1092.345148 | 1148.033981 | -4.8508 | 0.098434 / 5.00156 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 1514.141483 | 1148.033981 | 31.889953 | 0.003927 / 5.00156 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1987.487869 | 1148.033981 | 73.120997 | 0.000466 / 5.00156 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=500, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 1235.847647 | 1312.857419 | -5.865814 | 0.150523 / 5.000138 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 1575.398434 | 1312.857419 | 19.997679 | 5.047346 / 5.000138 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 2108.520396 | 1312.857419 | 60.605437 | 0.002758 / 5.000138 | ortools-gls x1 | 0 |
| rand+nn | 1 | 3102.748183 | 1312.857419 | 136.335503 | 0.000509 / 5.000138 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=500, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 1153.328124 | 1197.855587 | -3.717265 | 0.218552 / 5.000598 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 1158.791673 | 1197.855587 | -3.261154 | 0.793623 / 5.000598 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 2903.318828 | 1197.855587 | 142.376365 | 0.001884 / 5.000598 | ortools-gls x1 | 0 |
| rand+nn | 1 | 4162.728004 | 1197.855587 | 247.515013 | 0.00038 / 5.000598 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=1000, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 3043.069658 | 3349.333293 | -9.144018 | 0.584709 / 5.001683 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 3056.223966 | 3349.333293 | -8.751274 | 3.985301 / 5.001683 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 5394.445828 | 3349.333293 | 61.060287 | 0.018265 / 5.001683 | ortools-gls x1 | 0 |
| rand+nn | 1 | 7218.462161 | 3349.333293 | 115.519374 | 0.000879 / 5.001683 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=1000, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 2280.616711 | 2496.622933 | -8.651936 | 2.614754 / 5.005167 | ortools-gls x1 | 1 |
| lkh-wrapper-v2 | 1 | 2302.508044 | 2496.622933 | -7.775098 | 0.777858 / 5.005167 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 4621.539536 | 2496.622933 | 85.111635 | 0.007327 / 5.005167 | ortools-gls x1 | 0 |
| rand+nn | 1 | 7126.829689 | 2496.622933 | 185.458793 | 0.000758 / 5.005167 | ortools-gls x1 | 0 |

### clustered-offset-depot | n=1000, m=7

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 2456.384726 | 2670.83384 | -8.029294 | 2.659203 / 5.001761 | ortools-gls x1 | 1 |
| lkh-wrapper-v2 | 1 | 2458.212251 | 2670.83384 | -7.960869 | 0.589192 / 5.001761 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 7412.768293 | 2670.83384 | 177.545094 | 0.010484 / 5.001761 | ortools-gls x1 | 0 |
| rand+nn | 1 | 10732.858942 | 2670.83384 | 301.854237 | 0.001428 / 5.001761 | ortools-gls x1 | 0 |

### outliers | n=20, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 45.227535 | 40.940981 | 10.470081 | 0.000503 / 5.000129 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 45.227535 | 40.940981 | 10.470081 | 0.000358 / 5.000129 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 52.467898 | 40.940981 | 28.15496 | 3.5e-05 / 5.000129 | ortools-gls x1 | 0 |
| rand+nn | 1 | 77.168288 | 40.940981 | 88.486661 | 2e-05 / 5.000129 | ortools-gls x1 | 0 |

### outliers | n=20, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 47.434064 | 39.730715 | 19.388901 | 0.000285 / 5.000884 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 47.434064 | 39.730715 | 19.388901 | 0.000269 / 5.000884 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 70.063848 | 39.730715 | 76.346809 | 2.4e-05 / 5.000884 | ortools-gls x1 | 0 |
| rand+nn | 1 | 93.634037 | 39.730715 | 135.671664 | 3.5e-05 / 5.000884 | ortools-gls x1 | 0 |

### outliers | n=20, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 56.350979 | 42.426809 | 32.819272 | 0.000692 / 5.000094 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 63.896416 | 42.426809 | 50.603869 | 0.000525 / 5.000094 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 100.333217 | 42.426809 | 136.485419 | 3.1e-05 / 5.000094 | ortools-gls x1 | 0 |
| rand+nn | 1 | 129.345412 | 42.426809 | 204.86717 | 2.2e-05 / 5.000094 | ortools-gls x1 | 0 |

### outliers | n=50, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| 2opt+greed | 1 | 147.437677 | 128.758543 | 14.507103 | 5.5e-05 / 5.001006 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 150.219013 | 128.758543 | 16.66722 | 0.001754 / 5.001006 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 151.157532 | 128.758543 | 17.396119 | 0.001591 / 5.001006 | ortools-gls x1 | 0 |
| rand+nn | 1 | 212.90777 | 128.758543 | 65.354286 | 3.5e-05 / 5.001006 | ortools-gls x1 | 0 |

### outliers | n=50, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 152.35931 | 125.643977 | 21.262725 | 0.001685 / 5.000849 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 153.145088 | 125.643977 | 21.888125 | 0.001627 / 5.000849 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 205.045313 | 125.643977 | 63.195497 | 5.3e-05 / 5.000849 | ortools-gls x1 | 0 |
| rand+nn | 1 | 297.06253 | 125.643977 | 136.43197 | 3.1e-05 / 5.000849 | ortools-gls x1 | 0 |

### outliers | n=50, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 144.103889 | 118.857456 | 21.240933 | 0.001342 / 5.000894 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 148.198835 | 118.857456 | 24.686191 | 0.002696 / 5.000894 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 307.224874 | 118.857456 | 158.481785 | 4.3e-05 / 5.000894 | ortools-gls x1 | 0 |
| rand+nn | 1 | 443.197463 | 118.857456 | 272.881498 | 3.3e-05 / 5.000894 | ortools-gls x1 | 0 |

### outliers | n=100, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 344.136105 | 311.797562 | 10.371647 | 0.019219 / 5.000614 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 344.564202 | 311.797562 | 10.508947 | 0.019293 / 5.000614 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 409.547369 | 311.797562 | 31.350408 | 0.000176 / 5.000614 | ortools-gls x1 | 0 |
| rand+nn | 1 | 516.011217 | 311.797562 | 65.495591 | 4.4e-05 / 5.000614 | ortools-gls x1 | 0 |

### outliers | n=100, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 381.565758 | 337.397732 | 13.090789 | 0.006056 / 5.000973 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 382.784485 | 337.397732 | 13.452003 | 0.005485 / 5.000973 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 588.205475 | 337.397732 | 74.33593 | 0.000133 / 5.000973 | ortools-gls x1 | 0 |
| rand+nn | 1 | 732.924752 | 337.397732 | 117.228713 | 4.7e-05 / 5.000973 | ortools-gls x1 | 0 |

### outliers | n=100, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 375.202466 | 291.572236 | 28.682508 | 0.009944 / 5.001307 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 376.230753 | 291.572236 | 29.035178 | 0.006329 / 5.001307 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 579.710233 | 291.572236 | 98.822165 | 0.000142 / 5.001307 | ortools-gls x1 | 0 |
| rand+nn | 1 | 871.757932 | 291.572236 | 198.985234 | 5.3e-05 / 5.001307 | ortools-gls x1 | 0 |

### outliers | n=200, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 688.517406 | 703.807369 | -2.172464 | 0.049703 / 5.000986 | ortools-gls x1 | 1 |
| lkh-wrapper-v3 | 1 | 690.340588 | 703.807369 | -1.913419 | 0.046413 / 5.000986 | ortools-gls x1 | 1 |
| 2opt+greed | 1 | 832.958638 | 703.807369 | 18.350372 | 0.000859 / 5.000986 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1134.12252 | 703.807369 | 61.141041 | 8.8e-05 / 5.000986 | ortools-gls x1 | 0 |

### outliers | n=200, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 777.79355 | 703.734285 | 10.523754 | 0.031145 / 5.000817 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 786.97613 | 703.734285 | 11.82859 | 0.015177 / 5.000817 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 988.124395 | 703.734285 | 40.411575 | 0.000613 / 5.000817 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1640.377685 | 703.734285 | 133.096173 | 9.4e-05 / 5.000817 | ortools-gls x1 | 0 |

### outliers | n=200, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 775.404178 | 683.154824 | 13.503433 | 0.029409 / 5.000387 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 778.639681 | 683.154824 | 13.977045 | 0.015332 / 5.000387 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 960.529527 | 683.154824 | 40.602027 | 0.000386 / 5.000387 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1816.706304 | 683.154824 | 165.928929 | 8e-05 / 5.000387 | ortools-gls x1 | 0 |

### outliers | n=500, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 2605.606782 | 2446.492576 | 6.503768 | 0.835974 / 0.275138 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 2627.613926 | 2446.492576 | 7.403307 | 0.631995 / 0.275138 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 2849.962334 | 2446.492576 | 16.491763 | 0.004047 / 0.275138 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 4251.056821 | 2446.492576 | 73.76128 | 0.000322 / 0.275138 | tsp-transform-lkh x1 | 0 |

### outliers | n=500, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 2410.190135 | 2489.124676 | -3.171177 | 0.685381 / 0.283556 | tsp-transform-lkh x1 | 1 |
| lkh-wrapper-v3 | 1 | 2497.445736 | 2489.124676 | 0.334297 | 0.166614 / 0.283556 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 3016.514751 | 2489.124676 | 21.187773 | 0.002605 / 0.283556 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 5050.941529 | 2489.124676 | 102.920391 | 0.0003 / 0.283556 | tsp-transform-lkh x1 | 0 |

### outliers | n=500, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 2843.224081 | 2650.033172 | 7.290132 | 0.716507 / 5.002012 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 2982.625769 | 2650.033172 | 12.550507 | 0.108574 / 5.002012 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 4268.532007 | 2650.033172 | 61.074663 | 0.002003 / 5.002012 | ortools-gls x1 | 0 |
| rand+nn | 1 | 7682.127763 | 2650.033172 | 189.887985 | 0.000471 / 5.002012 | ortools-gls x1 | 0 |

### outliers | n=1000, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 6157.225233 | 6079.836206 | 1.27288 | 5.65625 / 1.163558 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 6219.902303 | 6079.836206 | 2.303781 | 1.125027 / 1.163558 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 9160.401418 | 6079.836206 | 50.668556 | 0.016724 / 1.163558 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 13042.030767 | 6079.836206 | 114.512864 | 0.001 / 1.163558 | tsp-transform-lkh x1 | 0 |

### outliers | n=1000, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 7550.050132 | 7414.94928 | 1.822006 | 0.386681 / 1.041065 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 7615.128241 | 7414.94928 | 2.699667 | 5.346805 / 1.041065 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 10174.342415 | 7414.94928 | 37.213918 | 0.01207 / 1.041065 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 21576.702088 | 7414.94928 | 190.989207 | 0.001086 / 1.041065 | tsp-transform-lkh x1 | 0 |

### outliers | n=1000, m=7

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 6264.929195 | 6570.992205 | -4.65779 | 8.413867 / 1.205448 | tsp-transform-lkh x1 | 1 |
| lkh-wrapper-v3 | 1 | 6881.546723 | 6570.992205 | 4.726143 | 1.12048 / 1.205448 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 11771.828854 | 6570.992205 | 79.148422 | 0.008413 / 1.205448 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 22195.598506 | 6570.992205 | 237.781538 | 0.000845 / 1.205448 | tsp-transform-lkh x1 | 0 |

### mixed | n=20, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 49.017757 | 43.98713 | 11.436588 | 0.000388 / 5.000823 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 49.017757 | 43.98713 | 11.436588 | 0.000353 / 5.000823 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 53.150051 | 43.98713 | 20.830913 | 2.8e-05 / 5.000823 | ortools-gls x1 | 0 |
| rand+nn | 1 | 80.587943 | 43.98713 | 83.208004 | 2.1e-05 / 5.000823 | ortools-gls x1 | 0 |

### mixed | n=20, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 56.845812 | 43.353076 | 31.122903 | 0.000399 / 5.000106 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 56.845812 | 43.353076 | 31.122903 | 0.000334 / 5.000106 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 76.967857 | 43.353076 | 77.537246 | 2.4e-05 / 5.000106 | ortools-gls x1 | 0 |
| rand+nn | 1 | 101.604987 | 43.353076 | 134.36627 | 2.4e-05 / 5.000106 | ortools-gls x1 | 0 |

### mixed | n=20, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 64.080185 | 46.894674 | 36.647042 | 0.000379 / 4.99994 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 64.080185 | 46.894674 | 36.647042 | 0.000337 / 4.99994 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 83.831465 | 46.894674 | 78.765429 | 2.2e-05 / 4.99994 | ortools-gls x1 | 0 |
| rand+nn | 1 | 113.972726 | 46.894674 | 143.039809 | 2.1e-05 / 4.99994 | ortools-gls x1 | 0 |

### mixed | n=50, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 144.263839 | 122.181746 | 18.073152 | 0.001843 / 5.001038 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 144.773659 | 122.181746 | 18.490416 | 0.001527 / 5.001038 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 202.842932 | 122.181746 | 66.017379 | 5.8e-05 / 5.001038 | ortools-gls x1 | 0 |
| rand+nn | 1 | 215.582846 | 122.181746 | 76.444398 | 3.4e-05 / 5.001038 | ortools-gls x1 | 0 |

### mixed | n=50, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 148.061604 | 121.269654 | 22.092872 | 0.001346 / 5.001151 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 148.830184 | 121.269654 | 22.72665 | 0.002327 / 5.001151 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 248.076779 | 121.269654 | 104.566246 | 4.6e-05 / 5.001151 | ortools-gls x1 | 0 |
| rand+nn | 1 | 278.315266 | 121.269654 | 129.501163 | 3.2e-05 / 5.001151 | ortools-gls x1 | 0 |

### mixed | n=50, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 156.981465 | 119.507768 | 31.356704 | 0.00185 / 5.000917 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 156.981465 | 119.507768 | 31.356704 | 0.00151 / 5.000917 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 288.392511 | 119.507768 | 141.316959 | 5.7e-05 / 5.000917 | ortools-gls x1 | 0 |
| rand+nn | 1 | 436.671215 | 119.507768 | 265.391491 | 4.3e-05 / 5.000917 | ortools-gls x1 | 0 |

### mixed | n=100, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 331.18917 | 294.80155 | 12.34309 | 0.007037 / 5.000455 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 355.110156 | 294.80155 | 20.457357 | 0.013961 / 5.000455 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 428.337788 | 294.80155 | 45.296993 | 0.000129 / 5.000455 | ortools-gls x1 | 0 |
| rand+nn | 1 | 505.110656 | 294.80155 | 71.339213 | 4.5e-05 / 5.000455 | ortools-gls x1 | 0 |

### mixed | n=100, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 343.240933 | 298.639469 | 14.934886 | 0.011425 / 5.000814 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 347.759125 | 298.639469 | 16.447811 | 0.004556 / 5.000814 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 448.437608 | 298.639469 | 50.160195 | 0.000139 / 5.000814 | ortools-gls x1 | 0 |
| rand+nn | 1 | 691.67947 | 298.639469 | 131.6102 | 5.6e-05 / 5.000814 | ortools-gls x1 | 0 |

### mixed | n=100, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 364.88925 | 295.744801 | 23.379768 | 0.01475 / 5.000202 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 366.897813 | 295.744801 | 24.058922 | 0.013693 / 5.000202 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 534.171614 | 295.744801 | 80.619105 | 0.000134 / 5.000202 | ortools-gls x1 | 0 |
| rand+nn | 1 | 849.479542 | 295.744801 | 187.233973 | 4.5e-05 / 5.000202 | ortools-gls x1 | 0 |

### mixed | n=200, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 804.444736 | 748.347468 | 7.496153 | 0.020724 / 0.160449 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 830.376435 | 748.347468 | 10.961348 | 0.017788 / 0.160449 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 940.150502 | 748.347468 | 25.630211 | 0.000636 / 0.160449 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 1322.13367 | 748.347468 | 76.673768 | 8.7e-05 / 0.160449 | tsp-transform-lkh x1 | 0 |

### mixed | n=200, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 769.94199 | 680.700528 | 13.110238 | 0.050741 / 5.000596 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 778.991878 | 680.700528 | 14.439735 | 0.018602 / 5.000596 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 913.878586 | 680.700528 | 34.255601 | 0.000388 / 5.000596 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1667.129751 | 680.700528 | 144.913833 | 8.6e-05 / 5.000596 | ortools-gls x1 | 0 |

### mixed | n=200, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 851.336957 | 657.216788 | 29.536703 | 0.020572 / 5.001897 | ortools-gls x1 | 0 |
| lkh-wrapper-v2 | 1 | 855.015287 | 657.216788 | 30.096386 | 0.047163 / 5.001897 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 1119.452321 | 657.216788 | 70.332277 | 0.000393 / 5.001897 | ortools-gls x1 | 0 |
| rand+nn | 1 | 2095.761323 | 657.216788 | 218.884326 | 7.7e-05 / 5.001897 | ortools-gls x1 | 0 |

### mixed | n=500, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 2723.213606 | 2688.46639 | 1.292455 | 0.225904 / 0.252892 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 2737.210796 | 2688.46639 | 1.813093 | 0.21098 / 0.252892 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 3153.984002 | 2688.46639 | 17.315359 | 0.003033 / 0.252892 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 4536.716054 | 2688.46639 | 68.74736 | 0.000301 / 0.252892 | tsp-transform-lkh x1 | 0 |

### mixed | n=500, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 2334.983214 | 2262.520664 | 3.202735 | 0.68065 / 0.321882 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 2365.335967 | 2262.520664 | 4.544281 | 0.125792 / 0.321882 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 2501.834712 | 2262.520664 | 10.57732 | 0.002259 / 0.321882 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 5014.609261 | 2262.520664 | 121.638164 | 0.000261 / 0.321882 | tsp-transform-lkh x1 | 0 |

### mixed | n=500, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 2449.305105 | 2339.268774 | 4.703877 | 0.348036 / 0.287486 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 2717.622345 | 2339.268774 | 16.17401 | 0.091957 / 0.287486 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 3637.60563 | 2339.268774 | 55.501825 | 0.001797 / 0.287486 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 6113.616769 | 2339.268774 | 161.347342 | 0.000265 / 0.287486 | tsp-transform-lkh x1 | 0 |

### mixed | n=1000, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 6577.680204 | 6410.216406 | 2.612452 | 8.72764 / 1.571695 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 6811.984385 | 6410.216406 | 6.26762 | 0.662613 / 1.571695 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 6959.53512 | 6410.216406 | 8.569425 | 0.014238 / 1.571695 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 13457.533581 | 6410.216406 | 109.938834 | 0.002347 / 1.571695 | tsp-transform-lkh x1 | 0 |

### mixed | n=1000, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 7353.367482 | 7083.128917 | 3.815243 | 8.298246 / 1.059956 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 7443.722548 | 7083.128917 | 5.090881 | 0.462535 / 1.059956 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 9047.291038 | 7083.128917 | 27.730148 | 0.00828 / 1.059956 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 19854.800932 | 7083.128917 | 180.311161 | 0.00088 / 1.059956 | tsp-transform-lkh x1 | 0 |

### mixed | n=1000, m=7

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 7285.841514 | 7083.552788 | 2.855752 | 12.170232 / 1.418884 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 7651.941065 | 7083.552788 | 8.024056 | 2.895788 / 1.418884 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 12042.166974 | 7083.552788 | 70.001796 | 0.011125 / 1.418884 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 23727.246992 | 7083.552788 | 234.962521 | 0.001115 / 1.418884 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=20, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 53.663231 | 38.770922 | 38.411026 | 0.000561 / 5.000913 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 53.663231 | 38.770922 | 38.411026 | 0.000361 / 5.000913 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 58.75819 | 38.770922 | 51.552212 | 2.3e-05 / 5.000913 | ortools-gls x1 | 0 |
| rand+nn | 1 | 73.545962 | 38.770922 | 89.693611 | 2e-05 / 5.000913 | ortools-gls x1 | 0 |

### high-m-stress | n=20, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 55.635805 | 40.928378 | 35.934546 | 0.000627 / 5.000537 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 55.635805 | 40.928378 | 35.934546 | 0.000405 / 5.000537 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 84.181462 | 40.928378 | 105.679937 | 2.4e-05 / 5.000537 | ortools-gls x1 | 0 |
| rand+nn | 1 | 94.375576 | 40.928378 | 130.587139 | 2.2e-05 / 5.000537 | ortools-gls x1 | 0 |

### high-m-stress | n=20, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 57.414282 | 42.757026 | 34.280345 | 0.000485 / 5.000463 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 57.414282 | 42.757026 | 34.280345 | 0.000428 / 5.000463 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 129.432435 | 42.757026 | 202.716178 | 3.1e-05 / 5.000463 | ortools-gls x1 | 0 |
| rand+nn | 1 | 130.456348 | 42.757026 | 205.110903 | 2.3e-05 / 5.000463 | ortools-gls x1 | 0 |

### high-m-stress | n=50, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 124.498505 | 115.712851 | 7.592635 | 0.003198 / 4.999885 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 124.793283 | 115.712851 | 7.847384 | 0.001316 / 4.999885 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 166.050404 | 115.712851 | 43.502128 | 5.7e-05 / 4.999885 | ortools-gls x1 | 0 |
| rand+nn | 1 | 203.49768 | 115.712851 | 75.864373 | 3.2e-05 / 4.999885 | ortools-gls x1 | 0 |

### high-m-stress | n=50, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 122.335912 | 103.399663 | 18.313647 | 0.001802 / 5.000191 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 122.335912 | 103.399663 | 18.313647 | 0.001853 / 5.000191 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 193.371045 | 103.399663 | 87.013226 | 4.5e-05 / 5.000191 | ortools-gls x1 | 0 |
| rand+nn | 1 | 257.400134 | 103.399663 | 148.937111 | 3.8e-05 / 5.000191 | ortools-gls x1 | 0 |

### high-m-stress | n=50, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 166.716658 | 127.99682 | 30.250625 | 0.002293 / 5.000749 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 166.716658 | 127.99682 | 30.250625 | 0.002365 / 5.000749 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 470.115895 | 127.99682 | 267.287168 | 4.4e-05 / 5.000749 | ortools-gls x1 | 0 |
| rand+nn | 1 | 515.678295 | 127.99682 | 302.883677 | 2.9e-05 / 5.000749 | ortools-gls x1 | 0 |

### high-m-stress | n=100, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 257.859257 | 244.322844 | 5.54038 | 0.006034 / 0.049191 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 260.581445 | 244.322844 | 6.654556 | 0.004735 / 0.049191 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 354.336358 | 244.322844 | 45.027928 | 0.000193 / 0.049191 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 415.010276 | 244.322844 | 69.86143 | 4.6e-05 / 0.049191 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=100, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 273.629503 | 245.799506 | 11.322235 | 0.023719 / 5.000441 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 289.269668 | 245.799506 | 17.685211 | 0.006779 / 5.000441 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 380.650044 | 245.799506 | 54.862005 | 0.000137 / 5.000441 | ortools-gls x1 | 0 |
| rand+nn | 1 | 586.014189 | 245.799506 | 138.411459 | 4.8e-05 / 5.000441 | ortools-gls x1 | 0 |

### high-m-stress | n=100, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 328.755038 | 292.367567 | 12.445796 | 0.008724 / 5.000938 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 342.820846 | 292.367567 | 17.256798 | 0.004895 / 5.000938 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 576.0599 | 292.367567 | 97.032765 | 0.000118 / 5.000938 | ortools-gls x1 | 0 |
| rand+nn | 1 | 991.575357 | 292.367567 | 239.153678 | 4.2e-05 / 5.000938 | ortools-gls x1 | 0 |

### high-m-stress | n=200, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 643.576837 | 624.649979 | 3.029994 | 0.021268 / 0.159079 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 648.23706 | 624.649979 | 3.776048 | 0.024636 / 0.159079 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 802.727477 | 624.649979 | 28.508365 | 0.000509 / 0.159079 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 1102.794637 | 624.649979 | 76.546014 | 9e-05 / 0.159079 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=200, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 656.793286 | 636.655704 | 3.163025 | 0.017103 / 0.164155 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 667.688172 | 636.655704 | 4.874294 | 0.025645 / 0.164155 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 958.583741 | 636.655704 | 50.565484 | 0.000489 / 0.164155 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 1186.35182 | 636.655704 | 86.341191 | 8.1e-05 / 0.164155 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=200, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 709.715607 | 614.370196 | 15.519212 | 0.053156 / 5.002155 | ortools-gls x1 | 0 |
| lkh-wrapper-v3 | 1 | 757.291785 | 614.370196 | 23.263106 | 0.024156 / 5.002155 | ortools-gls x1 | 0 |
| 2opt+greed | 1 | 1167.997908 | 614.370196 | 90.113048 | 0.000458 / 5.002155 | ortools-gls x1 | 0 |
| rand+nn | 1 | 1905.641089 | 614.370196 | 210.177984 | 7.3e-05 / 5.002155 | ortools-gls x1 | 0 |

### high-m-stress | n=500, m=2

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 2094.264539 | 2041.864749 | 2.566271 | 0.583549 / 0.402143 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 2436.42337 | 2041.864749 | 19.323445 | 0.005931 / 0.402143 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 2468.939369 | 2041.864749 | 20.915911 | 0.481574 / 0.402143 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 3647.007574 | 2041.864749 | 78.611614 | 0.000308 / 0.402143 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=500, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 2340.198558 | 2260.759745 | 3.51381 | 0.64174 / 0.249505 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 2341.721339 | 2260.759745 | 3.581168 | 0.224604 / 0.249505 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 2801.241819 | 2260.759745 | 23.907099 | 0.002747 / 0.249505 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 4873.286528 | 2260.759745 | 115.559682 | 0.000259 / 0.249505 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=500, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 2586.276064 | 2298.511024 | 12.519628 | 1.586712 / 0.296072 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 2601.9635 | 2298.511024 | 13.202133 | 0.519333 / 0.296072 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 3050.973887 | 2298.511024 | 32.73697 | 0.002281 / 0.296072 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 6872.010539 | 2298.511024 | 198.976619 | 0.000223 / 0.296072 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=1000, m=3

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 6054.015376 | 5432.318717 | 11.444407 | 5.251912 / 1.404259 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 6122.32691 | 5432.318717 | 12.701909 | 12.449084 / 1.404259 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 6932.807166 | 5432.318717 | 27.62151 | 0.018936 / 1.404259 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 11924.935888 | 5432.318717 | 119.51834 | 0.003501 / 1.404259 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=1000, m=5

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v2 | 1 | 6417.445365 | 6360.087407 | 0.901842 | 3.175733 / 1.287928 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v3 | 1 | 6567.890434 | 6360.087407 | 3.267298 | 1.080262 / 1.287928 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 9805.833694 | 6360.087407 | 54.177656 | 0.012637 / 1.287928 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 18795.18534 | 6360.087407 | 195.517721 | 0.00155 / 1.287928 | tsp-transform-lkh x1 | 0 |

### high-m-stress | n=1000, m=7

| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| lkh-wrapper-v3 | 1 | 7683.69601 | 6204.469888 | 23.841297 | 3.502904 / 1.219815 | tsp-transform-lkh x1 | 0 |
| lkh-wrapper-v2 | 1 | 7929.797571 | 6204.469888 | 27.807818 | 14.68472 / 1.219815 | tsp-transform-lkh x1 | 0 |
| 2opt+greed | 1 | 9072.132756 | 6204.469888 | 46.219305 | 0.007608 / 1.219815 | tsp-transform-lkh x1 | 0 |
| rand+nn | 1 | 22654.978971 | 6204.469888 | 265.139639 | 0.000847 / 1.219815 | tsp-transform-lkh x1 | 0 |

## Key Observations

Best baseline usage across instances: `ortools-gls` - 76, `tsp-transform-lkh` - 32.

Overall comparable means:

- `lkh-wrapper-v2`: gap = 97.412334 | gap % = 11.780591 | time = 1.390729 / 3.706990 s over 108 runs.
- `lkh-wrapper-v3`: gap = 116.752956 | gap % = 13.240657 | time = 0.460469 / 3.706990 s over 108 runs.
- `2opt+greed`: gap = 711.230823 | gap % = 66.612430 | time = 0.002695 / 3.706990 s over 108 runs.
- `rand+nn`: gap = 2613.063994 | gap % = 144.421846 | time = 0.000311 / 3.706990 s over 108 runs.

Baseline candidate statuses:

- `tsp-transform-lkh::ok`: 108
- `ortools-gls::ok`: 102
- `exact-mip::optimal:SCIP`: 15
- `ortools-gls::no_solution`: 6
- `exact-mip::feasible:SCIP`: 3
