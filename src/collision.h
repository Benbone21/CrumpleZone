#pragma once
#include <utility>
#include <vector>

class Car;
namespace collision {
// All overlapping bounds, in car-index order so forces and wake propagation stay deterministic.
// The caller owns the scratch space; no allocations once the workload has warmed up.
void candidates(const std::vector<Car>& cars, std::vector<std::pair<int, int>>& pairs,
                std::vector<int>& order);
}
