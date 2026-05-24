#pragma once

#include <random>

namespace arctic {

struct RaceResult {
    bool agent_a_won;
    bool agent_b_won;
};

/**
 * RaceResolver handles the stochastic game mechanics when two agents
 * compete for the same trade opportunity.
 */
class RaceResolver {
public:
    /**
     * Resolves a tie when both agents decide to act simultaneously.
     * @param delta_a The latency drawn by Agent A for this step.
     * @param delta_b The latency drawn by Agent B for this step.
     * @return RaceResult indicating who won the race.
     */
    RaceResult resolve_race(double delta_a, double delta_b) const {
        if (delta_a < delta_b) {
            return {true, false}; // Agent A is faster
        } else if (delta_b < delta_a) {
            return {false, true}; // Agent B is faster
        }
        
        // Exact tie - theoretically probability zero with continuous distribution, 
        // but handled for completeness or discrete bounds.
        // We'll award it to neither, or coin flip. Let's do a deterministic flip based on a hash
        // or just return neither. Returning neither is safer.
        return {false, false}; 
    }
};

} // namespace arctic
