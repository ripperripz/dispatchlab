# Policy comparison: full-small-medium-large

Generated 2026-10-18T20:09:30Z on Apple M4 (AppleClang 21.0.0.21000334, Release). Dataset: Geofabrik asia/india/northern-zone 2026-10-17T20:20:51Z clip=[76.62,30.62,76.95,30.80] (142425 nodes). Routing: astar. Seeds: 1, 2, 3, 4, 5.

Every (size, traffic, seed) cell uses one pre-generated scenario shared by all policies (identical orders, locations, preparation times, courier starts, traffic). Values are mean ± sample std. dev. across seeds. "Wins" counts seeds where the policy's value is strictly better (lower; higher for completed orders).

## small (20 couriers, 100 orders), traffic=free

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 100.00 ± 0.00 | 100.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 25.34 ± 1.73 | 25.39 ± 1.72 | 0.05 ± 0.16 | 2/5 |
| p95 delivery (min) | 34.37 ± 4.29 | 33.84 ± 3.35 | -0.52 ± 1.05 | 3/5 |
| avg wait for courier (min) | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| courier utilization | 0.35 ± 0.02 | 0.35 ± 0.02 | 0.00 ± 0.00 | 2/5 |
| total distance (km) | 1231.95 ± 130.75 | 1268.26 ± 134.65 | 36.31 ± 21.78 | 0/5 |
| assignment latency mean (us) | 0.73 ± 0.21 | 12045.07 ± 432.43 | 12044.34 ± 432.51 | 0/5 |

## small (20 couriers, 100 orders), traffic=moderate

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 100.00 ± 0.00 | 100.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 26.81 ± 1.71 | 26.83 ± 1.87 | 0.02 ± 0.40 | 3/5 |
| p95 delivery (min) | 36.96 ± 4.47 | 37.06 ± 4.65 | 0.09 ± 1.62 | 3/5 |
| avg wait for courier (min) | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| courier utilization | 0.37 ± 0.02 | 0.37 ± 0.03 | 0.00 ± 0.01 | 3/5 |
| total distance (km) | 1242.39 ± 113.82 | 1261.64 ± 127.76 | 19.24 ± 32.34 | 2/5 |
| assignment latency mean (us) | 0.75 ± 0.09 | 11754.38 ± 723.50 | 11753.64 ± 723.53 | 0/5 |

## small (20 couriers, 100 orders), traffic=heavy

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 100.00 ± 0.00 | 100.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 33.61 ± 4.43 | 33.79 ± 4.74 | 0.19 ± 1.22 | 3/5 |
| p95 delivery (min) | 52.90 ± 9.66 | 53.57 ± 12.63 | 0.67 ± 5.08 | 2/5 |
| avg wait for courier (min) | 0.29 ± 0.60 | 0.36 ± 0.78 | 0.07 ± 0.19 | 1/5 |
| courier utilization | 0.46 ± 0.06 | 0.46 ± 0.06 | 0.00 ± 0.02 | 4/5 |
| total distance (km) | 1383.16 ± 224.29 | 1403.35 ± 227.92 | 20.19 ± 65.64 | 2/5 |
| assignment latency mean (us) | 0.46 ± 0.05 | 11243.15 ± 1058.66 | 11242.69 ± 1058.64 | 0/5 |

## small_peak (20 couriers, 100 orders), traffic=free

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 100.00 ± 0.00 | 100.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 41.70 ± 8.20 | 42.21 ± 7.08 | 0.51 ± 2.16 | 3/5 |
| p95 delivery (min) | 63.33 ± 14.81 | 63.80 ± 9.24 | 0.48 ± 5.90 | 3/5 |
| avg wait for courier (min) | 11.95 ± 6.18 | 12.42 ± 5.65 | 0.47 ± 1.40 | 2/5 |
| courier utilization | 0.62 ± 0.05 | 0.62 ± 0.04 | 0.00 ± 0.02 | 3/5 |
| total distance (km) | 1886.23 ± 137.28 | 1899.67 ± 81.92 | 13.44 ± 87.93 | 3/5 |
| assignment latency mean (us) | 0.26 ± 0.03 | 8275.14 ± 476.94 | 8274.89 ± 476.93 | 0/5 |

## small_peak (20 couriers, 100 orders), traffic=moderate

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 100.00 ± 0.00 | 100.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 49.18 ± 8.91 | 50.87 ± 8.42 | 1.68 ± 4.05 | 2/5 |
| p95 delivery (min) | 75.80 ± 13.66 | 80.15 ± 13.28 | 4.35 ± 8.07 | 1/5 |
| avg wait for courier (min) | 16.58 ± 6.96 | 17.85 ± 6.39 | 1.27 ± 2.37 | 2/5 |
| courier utilization | 0.68 ± 0.04 | 0.69 ± 0.05 | 0.01 ± 0.04 | 3/5 |
| total distance (km) | 1851.54 ± 127.38 | 1887.40 ± 123.90 | 35.86 ± 121.38 | 3/5 |
| assignment latency mean (us) | 0.27 ± 0.05 | 8440.48 ± 712.60 | 8440.21 ± 712.64 | 0/5 |

## small_peak (20 couriers, 100 orders), traffic=heavy

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 98.20 ± 1.48 | 96.40 ± 2.51 | -1.80 ± 1.10 | 0/5 |
| undelivered orders | 1.80 ± 1.48 | 3.60 ± 2.51 | 1.80 ± 1.10 | 0/5 |
| avg delivery (min) | 72.25 ± 8.10 | 72.87 ± 6.19 | 0.62 ± 3.49 | 2/5 |
| p95 delivery (min) | 114.99 ± 13.02 | 117.70 ± 8.30 | 2.71 ± 6.81 | 1/5 |
| avg wait for courier (min) | 32.10 ± 6.71 | 32.92 ± 5.99 | 0.82 ± 2.47 | 2/5 |
| courier utilization | 0.86 ± 0.04 | 0.87 ± 0.04 | 0.02 ± 0.02 | 1/5 |
| total distance (km) | 1872.51 ± 105.35 | 1917.16 ± 83.53 | 44.65 ± 65.53 | 0/5 |
| assignment latency mean (us) | 0.26 ± 0.04 | 8220.23 ± 729.60 | 8219.96 ± 729.61 | 0/5 |

## medium (100 couriers, 1000 orders), traffic=free

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 1000.00 ± 0.00 | 1000.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 24.10 ± 0.23 | 23.99 ± 0.23 | -0.11 ± 0.04 | 5/5 |
| p95 delivery (min) | 32.53 ± 0.62 | 32.33 ± 0.62 | -0.20 ± 0.15 | 5/5 |
| avg wait for courier (min) | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| courier utilization | 0.40 ± 0.00 | 0.40 ± 0.00 | -0.00 ± 0.00 | 5/5 |
| total distance (km) | 9571.71 ± 240.72 | 9494.33 ± 260.43 | -77.38 ± 39.17 | 5/5 |
| assignment latency mean (us) | 1.78 ± 0.19 | 14271.11 ± 114.13 | 14269.33 ± 114.03 | 0/5 |

## medium (100 couriers, 1000 orders), traffic=moderate

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 1000.00 ± 0.00 | 1000.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 25.07 ± 0.27 | 24.97 ± 0.25 | -0.10 ± 0.06 | 5/5 |
| p95 delivery (min) | 34.11 ± 0.69 | 33.75 ± 0.63 | -0.35 ± 0.34 | 4/5 |
| avg wait for courier (min) | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| courier utilization | 0.42 ± 0.00 | 0.42 ± 0.00 | -0.00 ± 0.00 | 5/5 |
| total distance (km) | 9486.57 ± 255.54 | 9454.28 ± 207.17 | -32.29 ± 68.71 | 3/5 |
| assignment latency mean (us) | 1.93 ± 0.18 | 14179.87 ± 159.48 | 14177.94 ± 159.47 | 0/5 |

## medium (100 couriers, 1000 orders), traffic=heavy

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 1000.00 ± 0.00 | 1000.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 28.11 ± 0.36 | 27.95 ± 0.36 | -0.15 ± 0.10 | 4/5 |
| p95 delivery (min) | 39.01 ± 1.24 | 38.59 ± 1.03 | -0.42 ± 0.37 | 5/5 |
| avg wait for courier (min) | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| courier utilization | 0.47 ± 0.01 | 0.47 ± 0.01 | -0.00 ± 0.00 | 4/5 |
| total distance (km) | 9561.95 ± 250.57 | 9544.32 ± 281.03 | -17.63 ± 85.63 | 2/5 |
| assignment latency mean (us) | 2.21 ± 0.88 | 14351.35 ± 138.22 | 14349.14 ± 138.60 | 0/5 |

## medium_peak (100 couriers, 1000 orders), traffic=free

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 999.40 ± 1.34 | 1000.00 ± 0.00 | 0.60 ± 1.34 | 1/5 |
| undelivered orders | 0.60 ± 1.34 | 0.00 ± 0.00 | -0.60 ± 1.34 | 1/5 |
| avg delivery (min) | 55.21 ± 3.32 | 53.52 ± 3.68 | -1.69 ± 1.40 | 4/5 |
| p95 delivery (min) | 93.42 ± 6.93 | 91.28 ± 6.51 | -2.14 ± 3.10 | 4/5 |
| avg wait for courier (min) | 24.65 ± 2.77 | 23.22 ± 3.01 | -1.43 ± 1.18 | 4/5 |
| courier utilization | 0.85 ± 0.02 | 0.84 ± 0.02 | -0.01 ± 0.01 | 4/5 |
| total distance (km) | 19882.55 ± 494.52 | 19577.81 ± 568.22 | -304.74 ± 217.96 | 4/5 |
| assignment latency mean (us) | 0.36 ± 0.05 | 8761.36 ± 349.77 | 8761.00 ± 349.75 | 0/5 |

## medium_peak (100 couriers, 1000 orders), traffic=moderate

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 973.60 ± 15.08 | 967.60 ± 22.61 | -6.00 ± 7.84 | 1/5 |
| undelivered orders | 26.40 ± 15.08 | 32.40 ± 22.61 | 6.00 ± 7.84 | 1/5 |
| avg delivery (min) | 69.36 ± 2.89 | 69.46 ± 3.55 | 0.10 ± 1.14 | 4/5 |
| p95 delivery (min) | 120.13 ± 3.97 | 120.55 ± 6.73 | 0.42 ± 3.36 | 3/5 |
| avg wait for courier (min) | 37.33 ± 2.86 | 37.66 ± 3.98 | 0.33 ± 1.40 | 3/5 |
| courier utilization | 0.93 ± 0.01 | 0.93 ± 0.01 | 0.00 ± 0.00 | 2/5 |
| total distance (km) | 19497.89 ± 361.44 | 19641.27 ± 488.22 | 143.38 ± 233.03 | 1/5 |
| assignment latency mean (us) | 0.36 ± 0.07 | 9143.68 ± 392.14 | 9143.33 ± 392.08 | 0/5 |

## medium_peak (100 couriers, 1000 orders), traffic=heavy

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 769.40 ± 19.69 | 765.40 ± 19.74 | -4.00 ± 9.06 | 1/5 |
| undelivered orders | 230.60 ± 19.69 | 234.60 ± 19.74 | 4.00 ± 9.06 | 1/5 |
| avg delivery (min) | 94.53 ± 2.72 | 94.09 ± 3.09 | -0.44 ± 0.84 | 2/5 |
| p95 delivery (min) | 167.00 ± 5.09 | 165.71 ± 5.85 | -1.29 ± 1.41 | 4/5 |
| avg wait for courier (min) | 62.39 ± 2.33 | 61.97 ± 2.73 | -0.43 ± 0.84 | 2/5 |
| courier utilization | 0.97 ± 0.00 | 0.97 ± 0.00 | -0.00 ± 0.00 | 2/5 |
| total distance (km) | 16542.52 ± 154.79 | 16524.09 ± 139.74 | -18.43 ± 149.42 | 2/5 |
| assignment latency mean (us) | 0.37 ± 0.03 | 9014.70 ± 285.43 | 9014.33 ± 285.40 | 0/5 |

## large (500 couriers, 5000 orders), traffic=free

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 5000.00 ± 0.00 | 5000.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 23.99 ± 0.13 | 23.88 ± 0.13 | -0.12 ± 0.02 | 5/5 |
| p95 delivery (min) | 32.47 ± 0.23 | 32.25 ± 0.23 | -0.22 ± 0.06 | 5/5 |
| avg wait for courier (min) | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| courier utilization | 0.40 ± 0.00 | 0.40 ± 0.00 | -0.00 ± 0.00 | 5/5 |
| total distance (km) | 42268.01 ± 833.98 | 41497.22 ± 588.27 | -770.79 ± 341.15 | 5/5 |
| assignment latency mean (us) | 9.41 ± 1.12 | 17128.50 ± 53.88 | 17119.09 ± 52.91 | 0/5 |

## large (500 couriers, 5000 orders), traffic=moderate

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 5000.00 ± 0.00 | 5000.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 24.89 ± 0.14 | 24.76 ± 0.14 | -0.12 ± 0.02 | 5/5 |
| p95 delivery (min) | 33.72 ± 0.20 | 33.50 ± 0.22 | -0.22 ± 0.06 | 5/5 |
| avg wait for courier (min) | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| courier utilization | 0.41 ± 0.00 | 0.41 ± 0.00 | -0.00 ± 0.00 | 5/5 |
| total distance (km) | 41946.17 ± 825.65 | 41120.46 ± 595.12 | -825.72 ± 314.57 | 5/5 |
| assignment latency mean (us) | 9.14 ± 0.37 | 17071.09 ± 64.31 | 17061.95 ± 64.68 | 0/5 |

## large (500 couriers, 5000 orders), traffic=heavy

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 5000.00 ± 0.00 | 5000.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| undelivered orders | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| avg delivery (min) | 27.54 ± 0.17 | 27.34 ± 0.17 | -0.20 ± 0.05 | 5/5 |
| p95 delivery (min) | 37.77 ± 0.40 | 37.35 ± 0.30 | -0.42 ± 0.19 | 5/5 |
| avg wait for courier (min) | 0.00 ± 0.00 | 0.00 ± 0.00 | 0.00 ± 0.00 | 0/5 |
| courier utilization | 0.46 ± 0.00 | 0.46 ± 0.00 | -0.00 ± 0.00 | 5/5 |
| total distance (km) | 41859.11 ± 798.89 | 41189.60 ± 622.44 | -669.51 ± 301.91 | 5/5 |
| assignment latency mean (us) | 10.59 ± 1.27 | 21173.52 ± 139.03 | 21162.92 ± 139.95 | 0/5 |

## large_peak (500 couriers, 5000 orders), traffic=free

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 4998.80 ± 1.64 | 4999.20 ± 1.30 | 0.40 ± 1.52 | 1/5 |
| undelivered orders | 1.20 ± 1.64 | 0.80 ± 1.30 | -0.40 ± 1.52 | 1/5 |
| avg delivery (min) | 54.72 ± 3.78 | 53.97 ± 3.33 | -0.74 ± 1.08 | 4/5 |
| p95 delivery (min) | 92.56 ± 5.37 | 91.97 ± 4.23 | -0.59 ± 1.96 | 3/5 |
| avg wait for courier (min) | 24.30 ± 3.17 | 23.62 ± 2.83 | -0.68 ± 0.88 | 4/5 |
| courier utilization | 0.85 ± 0.02 | 0.84 ± 0.02 | -0.00 ± 0.01 | 3/5 |
| total distance (km) | 97769.10 ± 3808.22 | 97395.75 ± 3329.49 | -373.35 ± 965.61 | 3/5 |
| assignment latency mean (us) | 1.06 ± 0.20 | 10870.35 ± 386.70 | 10869.29 ± 386.51 | 0/5 |

## large_peak (500 couriers, 5000 orders), traffic=moderate

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 4880.80 ± 44.50 | 4868.80 ± 58.70 | -12.00 ± 23.38 | 2/5 |
| undelivered orders | 119.20 ± 44.50 | 131.20 ± 58.70 | 12.00 ± 23.38 | 2/5 |
| avg delivery (min) | 69.45 ± 3.47 | 69.47 ± 3.69 | 0.02 ± 0.72 | 1/5 |
| p95 delivery (min) | 120.32 ± 3.22 | 120.10 ± 3.52 | -0.22 ± 1.37 | 3/5 |
| avg wait for courier (min) | 37.25 ± 3.44 | 37.42 ± 3.70 | 0.18 ± 0.79 | 1/5 |
| courier utilization | 0.93 ± 0.01 | 0.93 ± 0.01 | -0.00 ± 0.00 | 2/5 |
| total distance (km) | 96937.63 ± 2655.71 | 96899.54 ± 2839.50 | -38.09 ± 690.73 | 2/5 |
| assignment latency mean (us) | 1.01 ± 0.10 | 10766.32 ± 361.41 | 10765.31 ± 361.35 | 0/5 |

## large_peak (500 couriers, 5000 orders), traffic=heavy

| metric | nearest | greedy_eta | greedy_eta - nearest (paired) | greedy_eta wins |
|---|---|---|---|---|
| completed orders | 3836.80 ± 41.48 | 3858.40 ± 41.48 | 21.60 ± 25.13 | 4/5 |
| undelivered orders | 1163.20 ± 41.48 | 1141.60 ± 41.48 | -21.60 ± 25.13 | 4/5 |
| avg delivery (min) | 94.58 ± 1.65 | 94.43 ± 1.79 | -0.15 ± 0.46 | 3/5 |
| p95 delivery (min) | 167.54 ± 1.45 | 166.66 ± 2.36 | -0.88 ± 1.38 | 3/5 |
| avg wait for courier (min) | 62.62 ± 1.42 | 62.52 ± 1.50 | -0.10 ± 0.40 | 3/5 |
| courier utilization | 0.97 ± 0.00 | 0.97 ± 0.00 | -0.00 ± 0.00 | 2/5 |
| total distance (km) | 82258.78 ± 381.08 | 82237.09 ± 470.58 | -21.69 ± 120.13 | 1/5 |
| assignment latency mean (us) | 1.10 ± 0.14 | 10545.49 ± 60.73 | 10544.39 ± 60.84 | 0/5 |

Courier utilization = busy time / (couriers × simulated horizon); lower is not automatically better (it also falls when fewer orders are completed). Assignment latency is wall-clock time and is the only metric that varies between identical runs.
