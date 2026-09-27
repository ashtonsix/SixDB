# Selected Orbital checks

Finite configured instances only. Authored schedules, abstraction and fault assumptions remain in the family reports. Case counts are not architectural coverage or implementation correctness.

complete: 42; expected_violation: 13; witnessed: 14

Elapsed time is for the selected attempt; recovered states belong to its checkpoint, not work repeated in that time.

| Case | Disposition | States | Recovered states | Attempt seconds |
| --- | --- | ---: | ---: | ---: |
| cycle-ordered | complete | 30 |  | 0.5675091459415853 |
| cycle-ordered-unrelated | complete | 30 |  | 0.6694287310820073 |
| bridge-ordered | complete | 7 |  | 0.6079597380012274 |
| bridge-ordered-blocks | expected_violation | 7 |  | 0.514888274949044 |
| chain-ordered | complete | 9 |  | 0.5093020179774612 |
| chain-ordered-blocks | expected_violation | 9 |  | 0.47049433598294854 |
| cancel-ordered | complete | 8 |  | 0.3529457808472216 |
| replay-delay-ordered | complete | 72 |  | 0.5152301820926368 |
| replay-compete-ordered | complete | 128 |  | 0.504852978978306 |
| cycle-eligible | expected_violation | 65 |  | 0.46382140298373997 |
| cycle-eligible-unrelated | expected_violation | 65 |  | 0.46048353984951973 |
| bridge-eligible | complete | 10 |  | 0.36044073407538235 |
| chain-eligible | complete | 33 |  | 0.45482786814682186 |
| cancel-eligible | complete | 8 |  | 0.3697348430287093 |
| replay-delay-eligible | complete | 72 |  | 0.46832302398979664 |
| replay-compete-eligible | complete | 128 |  | 0.5168569688685238 |
| deferred-eligible | expected_violation | 36 |  | 0.4207086768001318 |
| id-order-eligible | expected_violation | 64 |  | 0.4134990079328418 |
| cycle-drain | complete | 52 |  | 0.4200192380230874 |
| cycle-drain-unrelated | complete | 52 |  | 0.42485155910253525 |
| bridge-drain | complete | 10 |  | 0.4107731699477881 |
| chain-drain | complete | 33 |  | 0.41407545004040003 |
| cancel-drain | complete | 8 |  | 0.3574199869763106 |
| replay-delay-drain | complete | 72 |  | 0.4657932629343122 |
| replay-compete-drain | complete | 128 |  | 0.5547021361999214 |
| deferred-drain | expected_violation | 36 |  | 0.46848768508061767 |
| id-order-drain | expected_violation | 64 |  | 0.4082917820196599 |
| cycle-head | complete | 52 |  | 0.4607049080077559 |
| cycle-head-unrelated | expected_violation | 65 |  | 0.4555906788446009 |
| bridge-head | complete | 10 |  | 0.41211442300118506 |
| chain-head | complete | 33 |  | 0.46087252791039646 |
| cancel-head | complete | 8 |  | 0.35724140889942646 |
| replay-delay-head | complete | 72 |  | 0.46244656713679433 |
| replay-compete-head | complete | 128 |  | 0.5565422500949353 |
| deferred-head | expected_violation | 36 |  | 0.36739311809651554 |
| id-order-head | expected_violation | 64 |  | 0.4054296968970448 |
| younger-drain | complete | 7 |  | 0.35882465401664376 |
| younger-witness-drain | witnessed | 7 |  | 0.35993358213454485 |
| cycle-witness-drain | witnessed | 7 |  | 0.35865953494794667 |
| replay-witness-drain | witnessed | 72 |  | 0.4059461399447173 |
| younger-head | complete | 7 |  | 0.36432478902861476 |
| younger-witness-head | witnessed | 7 |  | 0.40690032206475735 |
| cycle-witness-head | witnessed | 7 |  | 0.405267994152382 |
| replay-witness-head | witnessed | 72 |  | 0.41124953096732497 |
| cycle-independent-witness | witnessed | 7 |  | 0.47198058501817286 |
| cancel-witness | witnessed | 8 |  | 0.4132475149817765 |
| cycle-narrow-eligible | complete | 65 |  | 0.42411295394413173 |
| cycle-narrow-head | complete | 65 |  | 0.40566806495189667 |
| dynamic-ordered | complete | 332527 |  | 34.39892950607464 |
| dynamic-eligible | complete | 340133 |  | 37.37279514200054 |
| dynamic-drain | complete | 340133 |  | 36.33338421396911 |
| dynamic-head | complete | 340133 |  | 36.66807323205285 |
| dynamic-drop-cancel | expected_violation | 287367 |  | 9.300256941933185 |
| dynamic-cancel-witness | witnessed | 308450 |  | 11.075175901176408 |
| acquisition-ordered | complete | 592 |  | 1.0411673979833722 |
| acquisition-eligible | complete | 592 |  | 1.046741307945922 |
| acquisition-drain | complete | 592 |  | 0.7819067370146513 |
| acquisition-head | complete | 592 |  | 0.8626091058831662 |
| acquisition-cancel | complete | 15040 |  | 9.103093120036647 |
| acquisition-cycle | expected_violation | 616 |  | 0.8221791321411729 |
| acquisition-cycle-traffic | complete | 616 |  | 0.7606931070331484 |
| acquisition-partial | witnessed | 34 |  | 0.8825807410757989 |
| acquisition-crossed-cancel | witnessed | 51 |  | 1.6527500469237566 |
| acquisition-cancelled-completion | witnessed | 1798 |  | 0.9135221699252725 |
| acquisition-wide-drain | complete | 11356 |  | 8.770244878018275 |
| acquisition-wide-ordered | complete | 9952 |  | 7.010633890982717 |
| acquisition-wide-cancel | complete | 66432 |  | 53.06844852399081 |
| acquisition-bypass-witness | witnessed | 701 |  | 0.6177337958943099 |
| acquisition-wide-cancel-witness | witnessed | 61716 |  | 5.417084445012733 |
