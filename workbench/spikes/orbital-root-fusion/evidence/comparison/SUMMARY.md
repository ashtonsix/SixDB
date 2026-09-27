# Selected Orbital checks

Finite configured instances only. Authored schedules, abstraction and fault assumptions remain in the family reports. Case counts are not architectural coverage or implementation correctness.

complete: 20; expected_violation: 4; witnessed: 7

Elapsed time is for the selected attempt; recovered states belong to its checkpoint, not work repeated in that time.

| Case | Disposition | States | Recovered states | Attempt seconds |
| --- | --- | ---: | ---: | ---: |
| explicit-healthy | complete | 135 |  | 2.2779899640008807 |
| explicit-missing | complete | 70 |  | 1.8795397030189633 |
| explicit-cancel-before | complete | 76 |  | 1.8744906899519265 |
| explicit-cancel-after | complete | 181 |  | 2.216822769958526 |
| explicit-cancel-race | complete | 372 |  | 3.2704424248076975 |
| explicit-recover-acquire | complete | 143 |  | 1.6165403719060123 |
| explicit-recover-hold-reply | complete | 147 |  | 2.632555315969512 |
| explicit-recover-registered | complete | 144 |  | 1.5071936300955713 |
| explicit-shared-late | complete | 136 |  | 2.2404132920783013 |
| fused-healthy | complete | 130 |  | 1.3775789651554078 |
| fused-missing | complete | 65 |  | 2.0721464578527957 |
| fused-cancel-before | complete | 71 |  | 1.367028740933165 |
| fused-cancel-after | complete | 181 |  | 2.6388423449825495 |
| fused-cancel-race | complete | 373 |  | 2.735827110009268 |
| fused-recover-acquire | complete | 138 |  | 2.077289928914979 |
| fused-recover-hold-reply | complete | 142 |  | 1.6228501049336046 |
| fused-recover-registered | complete | 139 |  | 2.3821614598855376 |
| fused-shared-late | complete | 131 |  | 1.5101463980972767 |
| fused-skip-projection | expected_violation | 43 |  | 1.6789818310644478 |
| explicit-skip-discovery | expected_violation | 29 |  | 1.2667423419188708 |
| fused-wrong-descriptor | expected_violation | 24 |  | 1.8271902319975197 |
| fused-early-retire | expected_violation | 66 |  | 2.029499453958124 |
| fused-recovery-witness | witnessed | 127 |  | 3.457458927994594 |
| fused-lost-reply-witness | witnessed | 130 |  | 2.0866103949956596 |
| fused-shared-late-witness | witnessed | 119 |  | 2.2812940990552306 |
| fused-success-race | witnessed | 361 |  | 2.894153303001076 |
| fused-abort-race | witnessed | 278 |  | 2.519277104875073 |
| explicit-recover-aborted | complete | 85 |  | 1.6634474119637161 |
| fused-recover-aborted | complete | 80 |  | 2.0536325748544186 |
| explicit-success-race | witnessed | 360 |  | 2.0274109579622746 |
| explicit-abort-race | witnessed | 277 |  | 2.476856289897114 |
