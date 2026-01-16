#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include "third_party/nlohmann/json.hpp"
#include "api_feedback.hpp"

namespace cord19 {

using json = nlohmann::json;
namespace fs = std::filesystem;

// Global stats tracker for API usage and performance with persistence
class StatsTracker {
public:
    StatsTracker(const fs::path& storage_path = "stats.json") 
        : stats_file_(storage_path)
        , total_searches_(0)
        , search_cache_hits_(0)
        , ai_overview_calls_(0)
        , ai_overview_cache_hits_(0)
        , ai_summary_calls_(0)
        , ai_summary_cache_hits_(0)
        , ai_api_calls_remaining_(10000) // Default: 10,000 API calls allowed
        , ai_api_calls_used_(0)
    {
        // Load existing stats from file
        load_from_file();
    }

    // Search stats
    void increment_searches() { 
        total_searches_++; 
        save_to_file();
    }
    
    void increment_search_cache_hits() { 
        search_cache_hits_++; 
        save_to_file();
    }
    
    // AI Overview stats
    void increment_ai_overview_calls() { 
        ai_overview_calls_++; 
        save_to_file();
    }
    
    void increment_ai_overview_cache_hits() { 
        ai_overview_cache_hits_++; 
        save_to_file();
    }
    
    // AI Summary stats
    void increment_ai_summary_calls() { 
        ai_summary_calls_++; 
        save_to_file();
    }
    
    void increment_ai_summary_cache_hits() { 
        ai_summary_cache_hits_++; 
        save_to_file();
    }
    
    // AI API calls remaining (decrements on actual API calls, not cache hits)
    // Thread-safe: uses atomic compare-and-swap to prevent going below 0
    void decrement_ai_api_calls() {
        int64_t current = ai_api_calls_remaining_.load();
        while (current > 0) {
            // Try to atomically swap current with current-1
            // If another thread modified it, retry with new value
            if (ai_api_calls_remaining_.compare_exchange_weak(current, current - 1)) {
                ai_api_calls_used_++; // Also track how many API calls have been used
                save_to_file(); // Persist after successful decrement
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
        save_to_file();
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
        
        // AI API calls remaining and used
        stats["ai_api_calls_remaining"] = ai_api_calls_remaining_.load();
        stats["ai_api_calls_used"] = ai_api_calls_used_.load();
        
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
    fs::path stats_file_;
    mutable std::mutex file_mutex_; // Protects file I/O operations
    
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
    std::atomic<int64_t> ai_api_calls_used_;
    
    // Load stats from file (called on initialization)
    void load_from_file() {
        std::lock_guard<std::mutex> lock(file_mutex_);
        
        if (!fs::exists(stats_file_)) {
            std::cout << "[stats] No existing stats file found at: " << stats_file_ << "\n";
            return;
        }
        
        try {
            std::ifstream ifs(stats_file_);
            if (!ifs.is_open()) {
                std::cerr << "[stats] Failed to open file: " << stats_file_ << "\n";
                return;
            }
            
            json j;
            ifs >> j;
            
            // Load each stat if it exists
            if (j.contains("total_searches")) {
                total_searches_ = j["total_searches"].get<int64_t>();
            }
            if (j.contains("search_cache_hits")) {
                search_cache_hits_ = j["search_cache_hits"].get<int64_t>();
            }
            if (j.contains("ai_overview_calls")) {
                ai_overview_calls_ = j["ai_overview_calls"].get<int64_t>();
            }
            if (j.contains("ai_overview_cache_hits")) {
                ai_overview_cache_hits_ = j["ai_overview_cache_hits"].get<int64_t>();
            }
            if (j.contains("ai_summary_calls")) {
                ai_summary_calls_ = j["ai_summary_calls"].get<int64_t>();
            }
            if (j.contains("ai_summary_cache_hits")) {
                ai_summary_cache_hits_ = j["ai_summary_cache_hits"].get<int64_t>();
            }
            if (j.contains("ai_api_calls_remaining")) {
                ai_api_calls_remaining_ = j["ai_api_calls_remaining"].get<int64_t>();
            }
            if (j.contains("ai_api_calls_used")) {
                ai_api_calls_used_ = j["ai_api_calls_used"].get<int64_t>();
            }
            
            std::cout << "[stats] Loaded stats from file:\n";
            std::cout << "  - Total searches: " << total_searches_ << "\n";
            std::cout << "  - AI API calls remaining: " << ai_api_calls_remaining_ << "\n";
            
        } catch (const std::exception& e) {
            std::cerr << "[stats] Error loading from file: " << e.what() << "\n";
        }
    }
    
    // Save stats to file (called after each stat change)
    void save_to_file() {
        std::lock_guard<std::mutex> lock(file_mutex_);
        
        try {
            json j;
            j["total_searches"] = total_searches_.load();
            j["search_cache_hits"] = search_cache_hits_.load();
            j["ai_overview_calls"] = ai_overview_calls_.load();
            j["ai_overview_cache_hits"] = ai_overview_cache_hits_.load();
            j["ai_summary_calls"] = ai_summary_calls_.load();
            j["ai_summary_cache_hits"] = ai_summary_cache_hits_.load();
            j["ai_api_calls_remaining"] = ai_api_calls_remaining_.load();
            j["ai_api_calls_used"] = ai_api_calls_used_.load();
            
            // Add timestamp
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            #ifdef _WIN32
            gmtime_s(&tm, &time_t);
            #else
            gmtime_r(&time_t, &tm);
            #endif
            
            char buffer[100];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
            j["last_updated"] = buffer;
            
            std::ofstream ofs(stats_file_);
            if (!ofs.is_open()) {
                std::cerr << "[stats] Failed to open file for writing: " << stats_file_ << "\n";
                return;
            }
            
            ofs << j.dump(2);
            ofs.close();
            
        } catch (const std::exception& e) {
            std::cerr << "[stats] Error saving to file: " << e.what() << "\n";
        }
    }
};

} // namespace cord19
