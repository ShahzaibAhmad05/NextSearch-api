#pragma once

#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "third_party/nlohmann/json.hpp"

// Forward declarations
namespace httplib {
    struct Request;
    struct Response;
}

namespace cord19 {

using json = nlohmann::json;
namespace fs = std::filesystem;

// Feedback storage configuration
constexpr size_t MAX_FEEDBACK_ENTRIES = 500;  // Maximum number of feedback entries to keep

// Feedback manager class to handle storage and retrieval
class FeedbackManager {
public:
    FeedbackManager(const fs::path& storage_path);
    
    // Add a new feedback entry (returns true on success)
    bool add_feedback(const json& feedback_data);
    
    // Get all feedback entries
    json get_all_feedback() const;
    
    // Get feedback count
    size_t get_count() const;

private:
    fs::path feedback_file_;
    mutable std::mutex mutex_;
    std::deque<json> feedback_entries_;
    
    // Load feedback from file
    void load_from_file();
    
    // Save feedback to file
    void save_to_file() const;
};

// Handle POST /api/feedback
void handle_feedback(FeedbackManager& manager, 
                     const httplib::Request& req, 
                     httplib::Response& res);

} // namespace cord19
