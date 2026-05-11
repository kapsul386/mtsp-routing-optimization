#pragma once

// Wall-clock budget used by every search loop in v21. Provides a cheap
// `ShouldStop` poll (decrement-then-check, so most iterations cost one int
// decrement) and a `ForceCheck` for periodic time gates. `SubBudget` carves
// out a time-capped child window without extending the parent deadline.

#include <chrono>
#include <limits>
#include <algorithm>

namespace mtsp::v21 {

class SearchBudget {
public:
    // total_budget_ms <= 0 disables the budget entirely (ShouldStop always false).
    // reserve_budget_ms is subtracted from total_budget_ms to leave time for
    // post-search cleanup (validation, sanitization, etc.).
    SearchBudget(int total_budget_ms, int reserve_budget_ms = 0, int polling_interval = 32)
        : enabled_(total_budget_ms > 0),
          polling_interval_(std::max(1, polling_interval)),
          polls_until_check_(std::max(1, polling_interval)) {
        if (!enabled_) {
            deadline_ = std::chrono::steady_clock::time_point::max();
            return;
        }
        const int eff = std::max(1, total_budget_ms - std::max(0, reserve_budget_ms));
        start_ = std::chrono::steady_clock::now();
        deadline_ = start_ + std::chrono::milliseconds(eff);
    }

    // Cheap stop check for inner loops: amortised one int decrement; only
    // queries the clock every `polling_interval` calls. Once the deadline has
    // been observed, all subsequent calls return true (sticky).
    bool ShouldStop() {
        if (!enabled_ || stop_requested_) return stop_requested_ || false;
        if (--polls_until_check_ > 0) return false;
        polls_until_check_ = polling_interval_;
        stop_requested_ = std::chrono::steady_clock::now() >= deadline_;
        return stop_requested_;
    }

    // Unconditional clock query — use at phase boundaries where the cost of
    // one extra clock read is negligible.
    bool ForceCheck() {
        if (!enabled_) return false;
        stop_requested_ = std::chrono::steady_clock::now() >= deadline_;
        return stop_requested_;
    }

    // Wall-clock milliseconds remaining before the deadline. Returns INT_MAX
    // when the budget is disabled (unlimited). Returns 0 when expired.
    int RemainingMs() const {
        if (!enabled_) return std::numeric_limits<int>::max();
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline_) return 0;
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - now).count());
    }

    // Wall-clock milliseconds elapsed since this budget was constructed.
    // Returns 0 when the budget is disabled.
    int ElapsedMs() const {
        if (!enabled_) return 0;
        const auto now = std::chrono::steady_clock::now();
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count());
    }

    // Returns true if a finite deadline is being tracked.
    bool Enabled() const { return enabled_; }

    // Returns a child budget whose deadline is min(parent_deadline, now + max_ms).
    // Useful for capping a single phase (e.g., one POPMUSIC call) without
    // letting it eat the rest of the parent budget.
    SearchBudget SubBudget(int max_ms) const {
        SearchBudget child(0);
        child.enabled_ = enabled_;
        child.start_ = std::chrono::steady_clock::now();
        if (enabled_) {
            const auto cap = child.start_ + std::chrono::milliseconds(std::max(1, max_ms));
            child.deadline_ = (cap < deadline_) ? cap : deadline_;
        } else {
            child.deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, max_ms));
            child.enabled_ = true;
        }
        child.polling_interval_ = polling_interval_;
        child.polls_until_check_ = polling_interval_;
        return child;
    }

private:
    bool enabled_ = false;
    std::chrono::steady_clock::time_point start_{};
    std::chrono::steady_clock::time_point deadline_{};
    int polling_interval_ = 32;
    int polls_until_check_ = 32;
    bool stop_requested_ = false;
};

}  // namespace mtsp::v21
