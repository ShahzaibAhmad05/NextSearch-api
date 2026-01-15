#include "api_feedback.hpp"
#include "api_http.hpp"
#include "third_party/httplib.h"

#include <chrono>
#include <iostream>

namespace cord19 {

FeedbackManager::FeedbackManager(const fs::path& storage_path) 
    : feedback_file_(storage_path) {
    // Create parent directory if it doesn't exist
    if (!storage_path.parent_path().empty()) {
        fs::create_directories(storage_path.parent_path());
    }
    
    // Load existing feedback from file
    load_from_file();
    
    std::cout << "[feedback] Initialized with " << feedback_entries_.size() 
              << " existing entries (max: " << MAX_FEEDBACK_ENTRIES << ")\n";
}

bool FeedbackManager::add_feedback(const json& feedback_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Add timestamp if not present
        json entry = feedback_data;
        if (!entry.contains("timestamp")) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            #ifdef _WIN32
            localtime_s(&tm, &time_t);
            #else
            localtime_r(&time_t, &tm);
            #endif
            
            char buffer[100];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
            
            // Add milliseconds
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
            
            std::string timestamp = std::string(buffer) + "." + 
                std::to_string(ms.count()).substr(0, 3) + "Z";
            entry["timestamp"] = timestamp;
        }
        
        // Add to deque
        feedback_entries_.push_back(entry);
        
        // Remove oldest if we exceed max entries
        while (feedback_entries_.size() > MAX_FEEDBACK_ENTRIES) {
            feedback_entries_.pop_front();
        }
        
        // Save to file
        save_to_file();
        
        std::cout << "[feedback] Added entry (type: " 
                  << entry.value("type", "unknown") 
                  << ", total: " << feedback_entries_.size() << ")\n";
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[feedback] Error adding entry: " << e.what() << "\n";
        return false;
    }
}

json FeedbackManager::get_all_feedback() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    json result;
    result["count"] = feedback_entries_.size();
    result["max_entries"] = MAX_FEEDBACK_ENTRIES;
    result["entries"] = json::array();
    
    for (const auto& entry : feedback_entries_) {
        result["entries"].push_back(entry);
    }
    
    return result;
}

size_t FeedbackManager::get_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return feedback_entries_.size();
}

void FeedbackManager::load_from_file() {
    if (!fs::exists(feedback_file_)) {
        std::cout << "[feedback] No existing feedback file found at: " 
                  << feedback_file_ << "\n";
        return;
    }
    
    try {
        std::ifstream ifs(feedback_file_);
        if (!ifs.is_open()) {
            std::cerr << "[feedback] Failed to open file: " << feedback_file_ << "\n";
            return;
        }
        
        json j;
        ifs >> j;
        
        if (j.contains("entries") && j["entries"].is_array()) {
            feedback_entries_.clear();
            for (const auto& entry : j["entries"]) {
                feedback_entries_.push_back(entry);
            }
            
            // Trim to max entries if loaded file has more
            while (feedback_entries_.size() > MAX_FEEDBACK_ENTRIES) {
                feedback_entries_.pop_front();
            }
        }
        
        std::cout << "[feedback] Loaded " << feedback_entries_.size() 
                  << " entries from file\n";
    } catch (const std::exception& e) {
        std::cerr << "[feedback] Error loading from file: " << e.what() << "\n";
    }
}

void FeedbackManager::save_to_file() const {
    try {
        json j;
        j["count"] = feedback_entries_.size();
        j["max_entries"] = MAX_FEEDBACK_ENTRIES;
        j["entries"] = json::array();
        
        for (const auto& entry : feedback_entries_) {
            j["entries"].push_back(entry);
        }
        
        std::ofstream ofs(feedback_file_);
        if (!ofs.is_open()) {
            std::cerr << "[feedback] Failed to open file for writing: " 
                      << feedback_file_ << "\n";
            return;
        }
        
        ofs << j.dump(2);
        ofs.close();
    } catch (const std::exception& e) {
        std::cerr << "[feedback] Error saving to file: " << e.what() << "\n";
    }
}

void handle_feedback(FeedbackManager& manager, 
                     const httplib::Request& req, 
                     httplib::Response& res) {
    enable_cors(res);
    
    try {
        // Parse request body
        json request_body = json::parse(req.body);
        
        // Validate required fields
        if (!request_body.contains("message") || !request_body["message"].is_string()) {
            res.status = 400;
            json err;
            err["error"] = "missing or invalid 'message' field";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        if (!request_body.contains("type") || !request_body["type"].is_string()) {
            res.status = 400;
            json err;
            err["error"] = "missing or invalid 'type' field";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        std::string type = request_body["type"];
        if (type != "anonymous" && type != "replyable") {
            res.status = 400;
            json err;
            err["error"] = "type must be 'anonymous' or 'replyable'";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Validate email field based on type
        if (type == "replyable") {
            if (!request_body.contains("email") || 
                request_body["email"].is_null() || 
                !request_body["email"].is_string() ||
                request_body["email"].get<std::string>().empty()) {
                res.status = 400;
                json err;
                err["error"] = "email is required for 'replyable' type feedback";
                res.set_content(err.dump(2), "application/json");
                return;
            }
        } else {
            // For anonymous, email should be null
            if (request_body.contains("email") && !request_body["email"].is_null()) {
                request_body["email"] = nullptr;
            }
        }
        
        // Add the feedback
        bool success = manager.add_feedback(request_body);
        
        if (success) {
            json response;
            response["success"] = true;
            response["message"] = "Feedback received successfully";
            response["total_count"] = manager.get_count();
            res.set_content(response.dump(2), "application/json");
        } else {
            res.status = 500;
            json err;
            err["error"] = "Failed to save feedback";
            res.set_content(err.dump(2), "application/json");
        }
        
    } catch (const json::parse_error& e) {
        res.status = 400;
        json err;
        err["error"] = "invalid JSON in request body";
        err["details"] = e.what();
        res.set_content(err.dump(2), "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        json err;
        err["error"] = "internal server error";
        err["details"] = e.what();
        res.set_content(err.dump(2), "application/json");
    }
}

} // namespace cord19
