#pragma once
#include "index.h"
#include <expected>
namespace directory_spike {
enum class PlacementPolicy { shift, hierarchical, midpoint };
struct PlacementEdit {std::vector<Row> changed;u64 moved{},window{},duplicate_moves{};};
class OrderedPlacement {
    unsigned j_{},suffix_bits_{};
    PlacementPolicy policy_;
    std::vector<Row> rows_;
public:
    OrderedPlacement(std::vector<Row>,unsigned j,PlacementPolicy,unsigned suffix_bits=0);
    std::expected<PlacementEdit,bool> insert(Row);
    Row erase(u64 id);
    const std::vector<Row>& rows()const{return rows_;}
    void verify()const;
};
}
