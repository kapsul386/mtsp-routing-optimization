#pragma once

#include "00_types.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace mtsp::v21 {

struct SaConfig {
    double T_frac_init = 0.05;
    double cooling = 0.995;
    int reheat_after_no_improvement = 200;
    double reheat_factor = 0.5;
    int pilot_moves = 1000;
    double T_clamp_lo_frac = 1e-4;  // T_init >= sum * 1e-4
    double T_clamp_hi_frac = 0.10;  // T_init <= sum * 0.10
};

class SaEngine {
public:
    SaEngine(SaConfig cfg, std::mt19937& rng) : cfg_(cfg), rng_(rng) {}

    // Initialize T from a baseline cost (e.g. current sum / max).
    void InitFromBaseline(double baseline) {
        T_init_ = std::max(1e-12, baseline * cfg_.T_frac_init);
        T_init_ = std::clamp(T_init_, baseline * cfg_.T_clamp_lo_frac, baseline * cfg_.T_clamp_hi_frac);
        T_ = T_init_;
        no_improve_streak_ = 0;
        reheats_ = 0;
        cooldowns_ = 0;
    }

    // Force initial T directly (used by Parallel Tempering to spread replicas
    // across a geometric T sequence). Bypasses Ben-Ameur / clamp logic.
    void ForceInit(double T_init) {
        T_init_ = std::max(1e-12, T_init);
        T_ = T_init_;
        no_improve_streak_ = 0;
        reheats_ = 0;
        cooldowns_ = 0;
    }

    // Auto-tune T0 via Ben-Ameur (2004): T0 = -avg(positive delta) / log(target_acceptance).
    // Caller supplies the average positive delta from a pilot run of cfg_.pilot_moves random moves.
    void AutoTuneT0(double avg_pos_delta, double target_acceptance = 0.5, double baseline_for_clamp = 0.0) {
        if (avg_pos_delta <= 0.0) {
            T_init_ = std::max(1e-12, baseline_for_clamp * cfg_.T_frac_init);
        } else {
            const double T = -avg_pos_delta / std::log(std::clamp(target_acceptance, 0.05, 0.95));
            T_init_ = T;
        }
        if (baseline_for_clamp > 0.0) {
            T_init_ = std::clamp(T_init_, baseline_for_clamp * cfg_.T_clamp_lo_frac, baseline_for_clamp * cfg_.T_clamp_hi_frac);
        }
        T_ = T_init_;
    }

    // Metropolis acceptance.
    bool Accept(double delta) {
        if (delta <= 0.0) return true;
        if (T_ <= 1e-12) return false;
        std::uniform_real_distribution<double> u(0.0, 1.0);
        return u(rng_) < std::exp(-delta / T_);
    }

    void Cooldown() {
        T_ *= cfg_.cooling;
        if (T_ < 1e-12) T_ = 1e-12;
        ++cooldowns_;
    }

    void Reheat() {
        T_ = T_init_ * cfg_.reheat_factor;
        no_improve_streak_ = 0;
        ++reheats_;
    }

    void NoteImprovement() { no_improve_streak_ = 0; }
    void NoteNoImprovement() { ++no_improve_streak_; }
    bool ShouldReheat() const { return no_improve_streak_ >= cfg_.reheat_after_no_improvement; }

    double Temperature() const { return T_; }
    double InitialTemperature() const { return T_init_; }
    int Reheats() const { return reheats_; }
    int Cooldowns() const { return cooldowns_; }
    int NoImproveStreak() const { return no_improve_streak_; }

private:
    SaConfig cfg_;
    std::mt19937& rng_;
    double T_init_ = 0.0;
    double T_ = 0.0;
    int no_improve_streak_ = 0;
    int reheats_ = 0;
    int cooldowns_ = 0;
};

}  // namespace mtsp::v21
