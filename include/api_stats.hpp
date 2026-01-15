#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include "third_party/nlohmann/json.hpp"
#include "api_feedback.hpp"

namespace cord19 {

using json = nlohmann::json;

// Global stats tracker for API usage and performance
class StatsTracker {
public:
    StatsTracker() 
        : total_searches_(0)
        , search_cache_hits_(0)
        , ai_overview_calls_(0)
        , ai_overview_cache_hits_(0)
        , ai_summary_calls_(0)
        , ai_summary_cache_hits_(0)
        , ai_api_calls_remaining_(10000) // Default: 10,000 API calls allowed
    {
    }

    // Search stats
    void increment_searches() { total_searches_++; }
    void increment_search_cache_hits() { search_cache_hits_++; }
    
    // AI Overview stats
    void increment_ai_overview_calls() { ai_overview_calls_++; }
    void increment_ai_overview_cache_hits() { ai_overview_cache_hits_++; }
    
    // AI Summary stats
    void increment_ai_summary_calls() { ai_summary_calls_++; }
    void increment_ai_summary_cache_hits() { ai_summary_cache_hits_++; }
    
    // AI API calls remaining (decrements on actual API calls, not cache hits)
    // Thread-safe: uses atomic compare-and-swap to prevent going below 0
    void decrement_ai_api_calls() {
        int64_t current = ai_api_calls_remaining_.load();
        while (current > 0) {
            // Try to atomically swap current with current-1
            // If another thread modified it, retry with new value
            if (ai_api_calls_remaining_.compare_exchange_weak(current, current - 1)) {
                return; // Success
            }
            // compare_exchange_weak updates 'current' with the actual value if it failed
            // Loop will retry or exit if current <= 0
        }
    }
    
    int64_t get_ai_api_calls_remaining() const {
        return ai_api_calls_remaining_.load();
    }
    
    void set_ai_api_calls_limit(int64_t limit) {
        ai_api_calls_remaining_ = limit;
    }
    
    // Generate stats JSON
    json get_stats_json(const FeedbackManager& feedback_manager) const {
        json stats;
        
        // Search stats
        stats["total_searches"] = total_searches_.load();
        stats["search_cache_hits"] = search_cache_hits_.load();
        
        // Calculate cache hit rate for searches
        int64_t total = total_searches_.load();
        int64_t hits = search_cache_hits_.load();
        stats["search_cache_hit_rate"] = (total > 0) ? (static_cast<double>(hits) / total) : 0.0;
        
        // AI Overview stats
        stats["ai_overview_calls"] = ai_overview_calls_.load();
        stats["ai_overview_cache_hits"] = ai_overview_cache_hits_.load();
        
        int64_t ai_overview_total = ai_overview_calls_.load();
        int64_t ai_overview_hits = ai_overview_cache_hits_.load();
        stats["ai_overview_cache_hit_rate"] = (ai_overview_total > 0) ? 
            (static_cast<double>(ai_overview_hits) / ai_overview_total) : 0.0;
        
        // AI Summary stats
        stats["ai_summary_calls"] = ai_summary_calls_.load();
        stats["ai_summary_cache_hits"] = ai_summary_cache_hits_.load();
        
        int64_t ai_summary_total = ai_summary_calls_.load();
        int64_t ai_summary_hits = ai_summary_cache_hits_.load();
        stats["ai_summary_cache_hit_rate"] = (ai_summary_total > 0) ? 
            (static_cast<double>(ai_summary_hits) / ai_summary_total) : 0.0;
        
        // AI API calls remaining
        stats["ai_api_calls_remaining"] = ai_api_calls_remaining_.load();
        
        // Last 10 feedback reviews
        json all_feedback = feedback_manager.get_all_feedback();
        json last_10_feedback = json::array();
        
        if (all_feedback.contains("entries") && all_feedback["entries"].is_array()) {
            const auto& entries = all_feedback["entries"];
            // Get last 10 entries (most recent at the end of the array)
            size_t start_idx = (entries.size() > 10) ? entries.size() - 10 : 0;
            for (size_t i = start_idx; i < entries.size(); i++) {
                last_10_feedback.push_back(entries[i]);
            }
        }
        
        stats["last_10_feedback"] = last_10_feedback;
        stats["total_feedback_count"] = all_feedback.value("count", 0);
        
        return stats;
    }

private:
    // Search metrics
    std::atomic<int64_t> total_searches_;
    std::atomic<int64_t> search_cache_hits_;
    
    // AI Overview metrics
    std::atomic<int64_t> ai_overview_calls_;
    std::atomic<int64_t> ai_overview_cache_hits_;
    
    // AI Summary metrics
    std::atomic<int64_t> ai_summary_calls_;
    std::atomic<int64_t> ai_summary_cache_hits_;
    
    // AI API quota
    std::atomic<int64_t> ai_api_calls_remaining_;
};

} // namespace cord19
