# Fleet comparison

256 MiB, base pages, isolated CPU. K ranges are the tested points within 15% of best in both independent-seed runs.

| Instance | Architecture | Screen / repeat K (10%) | Common K (15%) | Best ns/load range | Quick seconds | Lookup |
| --- | --- | --- | --- | --- | ---: | --- |
| c5a.xlarge | Zen 2 | [16, 16] | 16, 24, 32, 48, 64 | 9.52–9.80 | 7.07 | validated |
| c6a.xlarge | Zen 3 | [24, 24] | 24, 32, 64 | 6.67–6.72 | 6.93 | validated |
| c7a.large | Zen 4 | [24, 24] | 24, 32, 48, 64 | 8.04–8.22 | 7.59 | validated |
| c8a.large | Zen 5 | [32, 32] | 32, 48, 64 | 5.01–5.08 | 6.91 | validated |
| c8a.24xlarge | Zen 5 | [32, 32] | 32, 48, 64 | 4.97–5.04 | 6.84 | validated |
| c6i.xlarge | Ice Lake | [12, 12] | 12, 16, 24, 32, 48, 64 | 11.62–13.43 | 7.04 | validated |
| c7i.xlarge | Sapphire Rapids | [16, 16] | 16, 24, 32, 48, 64 | 11.20–11.26 | 7.93 | validated |
| c7i.48xlarge | Sapphire Rapids | [24, 16] | 16, 24, 32, 48, 64 | 9.26–10.13 | 7.65 | validated |
| c8i.xlarge | Granite Rapids | [64, 24] | 24, 64 | 7.76–7.76 | 6.85 | validated |
| c6g.large | Neoverse N1 | [16, 24] | 16, 24, 32, 48, 64 | 9.27–9.32 | 7.52 | validated |
| c7g.large | Neoverse V1 | [24, 24] | 24, 32, 48 | 6.72–6.72 | 6.95 | validated |
| c8g.large | Neoverse V2 | [24, 24] | 24, 32, 48, 64 | 6.97–7.11 | 6.90 | validated |
| i4i.xlarge | Ice Lake | [12, 12] | 12, 16, 24, 32, 48, 64 | 12.29–12.34 | 7.75 | validated |

## Filesystem I/O and transport observations

Medians from short tests, not sustained service limits or tail guarantees. Root filesystem unless marked instance store.

| Instance | 4 KiB direct read µs | Write + fdatasync µs | TCP loopback 64 B µs | HTTPS connect / TLS / first byte ms |
| --- | ---: | ---: | ---: | --- |
| c5a.xlarge | 284.4 | 629.2 | 59.1 | 4.00 / 29.15 / 31.22 |
| c6a.xlarge | 168.8 | 401.8 | 31.8 | 1.48 / 19.83 / 22.06 |
| c7a.large | 360.3 | 617.1 | 16.1 | 1.84 / 17.27 / 20.10 |
| c8a.large | 573.1 | 386.0 | 4.9 | 1.78 / 13.59 / 15.97 |
| c8a.24xlarge | 236.4 | 396.3 | 5.9 | 1.95 / 14.10 / 16.08 |
| c6i.xlarge | 256.9 | 619.0 | 27.4 | 1.95 / 23.48 / 26.40 |
| c7i.xlarge | 582.2 | 1076.3 | 8.5 | 1.64 / 17.43 / 21.46 |
| c7i.48xlarge | 526.3 | 637.9 | 19.3 | 2.11 / 17.79 / 20.12 |
| c8i.xlarge | 591.7 | 386.7 | 32.2 | 1.64 / 16.21 / 18.06 |
| c6g.large | 602.6 | 559.1 | 35.4 | 2.56 / 37.27 / 39.89 |
| c7g.large | 180.5 | 333.8 | 17.0 | 2.07 / 28.48 / 30.93 |
| c8g.large | 314.6 | 524.9 | 11.5 | 2.17 / 22.98 / 25.67 |
| i4i.xlarge | 248.0 | 409.3 | 23.8 | 1.34 / 21.96 / 23.85 |
| i4i.xlarge instance store | 101.6 | 34.3 | 23.6 | 1.30 / 21.93 / 23.69 |
