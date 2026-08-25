#pragma once

#include "ygo/simulation/simulation_contract.hpp"

namespace ygo::simulation {

SimulationResult run_canonical_simulation(const SimulationJob& job,
                                          const CanonicalSimulationConfig& config);

}  // namespace ygo::simulation
