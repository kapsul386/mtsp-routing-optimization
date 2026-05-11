#pragma once

// Simulated Annealing engine. Holds the temperature, cooling schedule, and
// reheat state; does not own the solution or the move logic. The pipeline
// asks SaEngine.Accept(delta) at each step and SaEngine tells it whether
// to accept a non-improving move. Initial T is set via Ben-Ameur's pilot
// formula (AutoTuneT0). Reheat fires on stagnation; the iter-count and
// wall-time triggers cover the two regimes (fast iters / slow iters
// respectively) with one mechanism.

#include "00_types.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace mtsp::v21 {

// Tunable knobs for SaEngine. See AutoTuneParams for the per-instance defaults.
struct SaConfig {
    double T_frac_init = 0.05;
    double cooling = 0.995;
    // Reheat fires when EITHER threshold is exceeded since the last improvement:
    //   - iter-count: classic ALNS streak (works on small/medium n where iter
    //     rate is high enough that the count saturates within a stagnation
    //     window).
    //   - wall-time ms: needed on very large n (>60k) where each iter takes
    //     >1s and the iter-count threshold rarely accumulates; a 30s wall
    //     gap reliably triggers diversification on plateau-prone seeds.
    // Set ms threshold to 0 to disable it (iter-count-only mode).
    int reheat_after_no_improvement = 200;
    int reheat_after_no_improvement_ms = 0;
    double reheat_factor = 0.5;
    int pilot_moves = 1000;
    double T_clamp_lo_frac = 1e-4;  // T_init >= sum * 1e-4
    double T_clamp_hi_frac = 0.10;  // T_init <= sum * 0.10
};

class SaEngine {
public:
    // Construct the engine with the given config. Must call InitFromBaseline or
    // ForceInit before the first Accept call.
    SaEngine(SaConfig cfg, std::mt19937& rng) : cfg_(cfg), rng_(rng) {}

    // Initialize T from a baseline cost (e.g. current sum / max).
    void InitFromBaseline(double baseline) {
        T_init_ = std::max(1e-12, baseline * cfg_.T_frac_init);
        T_init_ = std::clamp(T_init_, baseline * cfg_.T_clamp_lo_frac, baseline * cfg_.T_clamp_hi_frac);
        T_ = T_init_;
        no_improve_streak_ = 0;
        reheats_ = 0;
        cooldowns_ = 0;
        last_best_time_ = std::chrono::steady_clock::now();
    }

    // Force initial T directly (used by Parallel Tempering to spread replicas
    // across a geometric T sequence). Bypasses Ben-Ameur / clamp logic.
    void ForceInit(double T_init) {
        T_init_ = std::max(1e-12, T_init);
        T_ = T_init_;
        no_improve_streak_ = 0;
        reheats_ = 0;
        cooldowns_ = 0;
        last_best_time_ = std::chrono::steady_clock::now();
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

    // Multiply T by the cooling factor. Call once per accepted ALNS iteration.
    void Cooldown() {
        T_ *= cfg_.cooling;
        if (T_ < 1e-12) T_ = 1e-12;
        ++cooldowns_;
    }

    // Reset T to T_init_ * reheat_factor and restart the stagnation counters.
    void Reheat() {
        T_ = T_init_ * cfg_.reheat_factor;
        no_improve_streak_ = 0;
        last_best_time_ = std::chrono::steady_clock::now();
        ++reheats_;
    }

    // Called on any cost decrease (delta_real < 0): resets the iter-count
    // streak. Does NOT reset the wall-time clock — that one tracks time
    // since last BEST update, which is what plateau-detection actually wants.
    void NoteImprovement() { no_improve_streak_ = 0; }
    void NoteNoImprovement() { ++no_improve_streak_; }

    // Called from the pipeline on every BEST-cost update. Resets the wall-time
    // clock used by the time-based reheat trigger. We separate this from
    // NoteImprovement because on large n the algorithm can keep making small
    // non-best improvements for hundreds of seconds while best is stuck —
    // exactly the plateau case we want to escape.
    void NoteBestUpdate() {
        last_best_time_ = std::chrono::steady_clock::now();
    }

    long long MsSinceBestUpdate() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_best_time_).count();
    }

    bool ShouldReheat() const {
        if (no_improve_streak_ >= cfg_.reheat_after_no_improvement) return true;
        if (cfg_.reheat_after_no_improvement_ms > 0 &&
            MsSinceBestUpdate() >= cfg_.reheat_after_no_improvement_ms) {
            return true;
        }
        return false;
    }

    // Returns the current temperature T.
    double Temperature() const { return T_; }
    // Returns the initial temperature T_init set by the last Init* call.
    double InitialTemperature() const { return T_init_; }
    // Total number of reheats performed so far.
    int Reheats() const { return reheats_; }
    // Total number of Cooldown() calls so far.
    int Cooldowns() const { return cooldowns_; }
    // Iterations since the last NoteImprovement() call (iter-count stagnation measure).
    int NoImproveStreak() const { return no_improve_streak_; }

private:
    SaConfig cfg_;
    std::mt19937& rng_;
    double T_init_ = 0.0;
    double T_ = 0.0;
    int no_improve_streak_ = 0;
    int reheats_ = 0;
    int cooldowns_ = 0;
    std::chrono::steady_clock::time_point last_best_time_{};
};

}  // namespace mtsp::v21
