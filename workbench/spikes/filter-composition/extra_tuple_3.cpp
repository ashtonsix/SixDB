#include "extra.h"
void tuples_3() {
    extras::run_tuple<5, 3, 1, ikea::comparison(3)>();
    extras::run_tuple<8, 0, 1, ikea::comparison(3)>();
    extras::run_tuple<5, 3, 3, ikea::comparison(3)>();
}
