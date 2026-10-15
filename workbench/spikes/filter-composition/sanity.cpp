#include "extra.h"

int main() {
    runtime::check_only = true;
    extras::run_pair<3>();
    extras::run_pair<12>();
    extras::run_pair<64>();
    extras::run_tuple<5, 3, 1, ikea::comparison::equal>();
    extras::run_tuple<5, 3, 3, ikea::comparison::equal>();
    std::puts("shared contexts, joined/split range stages and TuplePack projections passed");
}
