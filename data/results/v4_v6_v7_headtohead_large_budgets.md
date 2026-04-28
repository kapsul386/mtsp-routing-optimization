# v4 vs v6 vs v7 on large mixed/uniform instances

## 250ms

| n | family | solver | valid | objective | gap % | time s | source |
| ---: | --- | --- | --- | ---: | ---: | ---: | --- |
| 25000 | mixed | lkh-wrapper-v4 | True | 21,699,945.761 | 44.766 | 9.244 | fresh-run |
| 25000 | mixed | lkh-wrapper-v6 | False | invalid |  | 0.946 | fresh-run |
| 25000 | mixed | lkh-wrapper-v7 | True | 14,989,653.242 | 0.000 | 0.254 | fresh-run |
| 25000 | uniform | lkh-wrapper-v4 | True | 67,575,871.674 | 81.276 | 9.315 | fresh-run |
| 25000 | uniform | lkh-wrapper-v6 | False | invalid |  | 0.724 | fresh-run |
| 25000 | uniform | lkh-wrapper-v7 | True | 37,277,847.267 | 0.000 | 0.241 | fresh-run |
| 50000 | mixed | lkh-wrapper-v4 | True | 80,263,174.740 | 0.000 | 19.987 | fresh-run |
| 50000 | mixed | lkh-wrapper-v6 | False | invalid |  | 2.074 | fresh-run |
| 50000 | mixed | lkh-wrapper-v7 | True | 121,448,434.905 | 51.313 | 0.401 | fresh-run |
| 50000 | uniform | lkh-wrapper-v4 | True | 272,936,401.225 | 41.900 | 20.783 | fresh-run |
| 50000 | uniform | lkh-wrapper-v6 | False | invalid |  | 1.853 | fresh-run |
| 50000 | uniform | lkh-wrapper-v7 | True | 192,344,362.894 | 0.000 | 0.312 | fresh-run |
| 100000 | mixed | lkh-wrapper-v4 | True | 364,230,527.804 | 0.000 | 46.595 | fresh-run |
| 100000 | mixed | lkh-wrapper-v6 | False | invalid |  | 6.125 | fresh-run |
| 100000 | mixed | lkh-wrapper-v7 | True | 499,517,361.648 | 37.143 | 1.021 | fresh-run |
| 100000 | uniform | lkh-wrapper-v4 | True | 1,091,147,106.333 | 0.000 | 45.816 | fresh-run |
| 100000 | uniform | lkh-wrapper-v6 | False | invalid |  | 5.198 | fresh-run |
| 100000 | uniform | lkh-wrapper-v7 | True | 1,094,112,397.377 | 0.272 | 0.550 | fresh-run |

## 1s

| n | family | solver | valid | objective | gap % | time s | source |
| ---: | --- | --- | --- | ---: | ---: | ---: | --- |
| 25000 | mixed | lkh-wrapper-v4 | True | 21,699,945.761 | 27.374 | 8.778 | fresh-run |
| 25000 | mixed | lkh-wrapper-v6 | False | invalid |  | 1.044 | fresh-run |
| 25000 | mixed | lkh-wrapper-v7 | True | 17,036,459.306 | 0.000 | 0.915 | fresh-run |
| 25000 | uniform | lkh-wrapper-v4 | True | 67,575,871.674 | 94.162 | 9.545 | fresh-run |
| 25000 | uniform | lkh-wrapper-v6 | False | invalid |  | 1.068 | fresh-run |
| 25000 | uniform | lkh-wrapper-v7 | True | 34,803,929.760 | 0.000 | 0.916 | fresh-run |
| 50000 | mixed | lkh-wrapper-v4 | True | 80,263,174.740 | 30.832 | 19.798 | fresh-run |
| 50000 | mixed | lkh-wrapper-v6 | False | invalid |  | 2.213 | fresh-run |
| 50000 | mixed | lkh-wrapper-v7 | True | 61,348,172.109 | 0.000 | 0.937 | fresh-run |
| 50000 | uniform | lkh-wrapper-v4 | True | 272,936,401.225 | 85.210 | 21.171 | fresh-run |
| 50000 | uniform | lkh-wrapper-v6 | False | invalid |  | 1.904 | fresh-run |
| 50000 | uniform | lkh-wrapper-v7 | True | 147,366,062.088 | 0.000 | 0.938 | fresh-run |
| 100000 | mixed | lkh-wrapper-v4 | True | 364,230,527.804 | 0.000 | 47.442 | fresh-run |
| 100000 | mixed | lkh-wrapper-v6 | False | invalid |  | 6.253 | fresh-run |
| 100000 | mixed | lkh-wrapper-v7 | True | 499,517,361.648 | 37.143 | 1.333 | fresh-run |
| 100000 | uniform | lkh-wrapper-v4 | True | 1,091,147,106.333 | 79.320 | 48.072 | fresh-run |
| 100000 | uniform | lkh-wrapper-v6 | False | invalid |  | 5.060 | fresh-run |
| 100000 | uniform | lkh-wrapper-v7 | True | 608,491,124.398 | 0.000 | 0.993 | fresh-run |

## 5s

| n | family | solver | valid | objective | gap % | time s | source |
| ---: | --- | --- | --- | ---: | ---: | ---: | --- |
| 25000 | mixed | lkh-wrapper-v4 | True | 21,699,945.761 | 453.955 | 10.410 | existing-summary |
| 25000 | mixed | lkh-wrapper-v6 | False | invalid |  | 5.017 | existing-summary |
| 25000 | mixed | lkh-wrapper-v7 | True | 3,917,279.034 | 0.000 | 4.754 | fresh-run |
| 25000 | uniform | lkh-wrapper-v4 | True | 67,575,871.674 | 573.548 | 10.791 | existing-summary |
| 25000 | uniform | lkh-wrapper-v6 | False | invalid |  | 5.141 | existing-summary |
| 25000 | uniform | lkh-wrapper-v7 | True | 10,032,829.069 | 0.000 | 4.761 | fresh-run |
| 50000 | mixed | lkh-wrapper-v4 | True | 80,263,174.740 | 379.314 | 26.573 | existing-summary |
| 50000 | mixed | lkh-wrapper-v6 | False | invalid |  | 4.970 | existing-summary |
| 50000 | mixed | lkh-wrapper-v7 | True | 16,745,428.771 | 0.000 | 4.668 | fresh-run |
| 50000 | uniform | lkh-wrapper-v4 | True | 272,936,401.225 | 522.413 | 24.731 | existing-summary |
| 50000 | uniform | lkh-wrapper-v6 | False | invalid |  | 5.241 | existing-summary |
| 50000 | uniform | lkh-wrapper-v7 | True | 43,851,328.488 | 0.000 | 4.806 | fresh-run |
| 100000 | mixed | lkh-wrapper-v4 | True | 364,230,527.804 | 373.143 | 80.775 | existing-summary |
| 100000 | mixed | lkh-wrapper-v6 | False | invalid |  | 6.304 | existing-summary |
| 100000 | mixed | lkh-wrapper-v7 | True | 76,981,147.568 | 0.000 | 4.592 | fresh-run |
| 100000 | uniform | lkh-wrapper-v4 | True | 1,091,147,106.333 | 509.367 | 60.129 | existing-summary |
| 100000 | uniform | lkh-wrapper-v6 | False | invalid |  | 7.109 | existing-summary |
| 100000 | uniform | lkh-wrapper-v7 | True | 179,062,485.719 | 0.000 | 4.646 | fresh-run |

## 30s

| n | family | solver | valid | objective | gap % | time s | source |
| ---: | --- | --- | --- | ---: | ---: | ---: | --- |
| 25000 | mixed | lkh-wrapper-v4 | True | 21,147,360.960 | 457.376 | 42.848 | existing-summary |
| 25000 | mixed | lkh-wrapper-v6 | False | invalid |  | 29.011 | existing-summary |
| 25000 | mixed | lkh-wrapper-v7 | True | 3,794,090.614 | 0.000 | 25.781 | fresh-run |
| 25000 | uniform | lkh-wrapper-v4 | True | 66,105,626.000 | 574.593 | 34.336 | existing-summary |
| 25000 | uniform | lkh-wrapper-v6 | False | invalid |  | 28.747 | existing-summary |
| 25000 | uniform | lkh-wrapper-v7 | True | 9,799,335.145 | 0.000 | 23.193 | fresh-run |
| 50000 | mixed | lkh-wrapper-v4 | True | 79,923,440.235 | 387.824 | 41.752 | existing-summary |
| 50000 | mixed | lkh-wrapper-v6 | False | invalid |  | 28.462 | existing-summary |
| 50000 | mixed | lkh-wrapper-v7 | True | 16,383,665.515 | 0.000 | 27.811 | fresh-run |
| 50000 | uniform | lkh-wrapper-v4 | True | 271,483,846.831 | 531.010 | 46.128 | existing-summary |
| 50000 | uniform | lkh-wrapper-v6 | False | invalid |  | 28.361 | existing-summary |
| 50000 | uniform | lkh-wrapper-v7 | True | 43,023,668.603 | 0.000 | 27.834 | fresh-run |
| 100000 | mixed | lkh-wrapper-v4 | True | 364,230,527.804 | 383.069 | 58.239 | existing-summary |
| 100000 | mixed | lkh-wrapper-v6 | False | invalid |  | 28.221 | existing-summary |
| 100000 | mixed | lkh-wrapper-v7 | True | 75,399,277.638 | 0.000 | 28.289 | fresh-run |
| 100000 | uniform | lkh-wrapper-v4 | True | 1,091,147,106.333 | 512.516 | 46.408 | existing-summary |
| 100000 | uniform | lkh-wrapper-v6 | False | invalid |  | 28.454 | existing-summary |
| 100000 | uniform | lkh-wrapper-v7 | True | 178,141,903.018 | 0.000 | 28.064 | fresh-run |

## Win Counts

| budget | solver | wins |
| --- | --- | ---: |
| 1s | lkh-wrapper-v4 | 1 |
| 1s | lkh-wrapper-v7 | 5 |
| 250ms | lkh-wrapper-v4 | 3 |
| 250ms | lkh-wrapper-v7 | 3 |
| 30s | lkh-wrapper-v7 | 6 |
| 5s | lkh-wrapper-v7 | 6 |

