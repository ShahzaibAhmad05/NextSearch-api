#include "api_ai_summary.hpp"
#include "api_ai_overview.hpp"
#include "api_engine.hpp"
#include "api_stats.hpp"
#include <iostream>
#include <sstream>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace cord19 {

// Helper function to construct the system prompt for AI summary generation
static std::string build_summary_system_prompt() {
    return R"(You are an AI assistant that generates short, informative summaries of scientific abstracts in proper markdown format with headings and newline chars.

    Your task is to analyze the provided abstract and create a clear summary that:

    1. Captures the main findings and key points
    2. Highlights the research objective and methodology if present
    3. Summarizes conclusions and implications
    4. Maintains scientific accuracy without speculation
    5. Uses clear, accessible language

    To SUCCEED, FOLLOW THIS RULE:
    - Format it in proper markdown with appropriate headings wherever needed.)";
}

// Helper function to build the user prompt with abstract
static std::string build_summary_user_prompt(const std::string& title, const std::string& abstract) {
    std::ostringstream oss;
    
    if (!title.empty()) {
        oss << "Document Title: " << title << "\n\n";
    }
    
    oss << "Abstract:\n" << abstract << "\n\n";
    oss << "Please provide a concise summary of this abstract.";
    
    return oss.str();
}

// Helper function to make HTTPS POST request using WinHTTP
static std::string make_https_post_summary(const std::string& url, const std::string& path, 
                                           const std::string& api_key, const std::string& body) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    std::string response;
    
    try {
        // Parse URL to extract host
        std::string host = url;
        if (host.find("https://") == 0) {
            host = host.substr(8);
        }
        if (host.back() == '/') {
            host.pop_back();
        }
        
        // Convert to wide strings
        std::wstring whost(host.begin(), host.end());
        std::wstring wpath(path.begin(), path.end());
        
        // Initialize WinHTTP
        hSession = WinHttpOpen(L"AzureOpenAI/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) return "";
        
        hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
                                     NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE);
        
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }
        
        // Set headers
        std::string headers_str = "Content-Type: application/json\r\n";
        headers_str += "api-key: " + api_key + "\r\n";
        std::wstring wheaders(headers_str.begin(), headers_str.end());
        
        // Send request
        BOOL bResults = WinHttpSendRequest(hRequest,
                                          wheaders.c_str(),
                                          (DWORD)-1,
                                          (LPVOID)body.c_str(),
                                          (DWORD)body.length(),
                                          (DWORD)body.length(),
                                          0);
        
        if (bResults) {
            bResults = WinHttpReceiveResponse(hRequest, NULL);
        }
        
        if (bResults) {
            DWORD dwSize = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                
                if (dwSize > 0) {
                    std::vector<char> buffer(dwSize + 1, 0);
                    DWORD dwDownloaded = 0;
                    if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                        response.append(buffer.data(), dwDownloaded);
                    }
                }
            } while (dwSize > 0);
        }
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
    } catch (...) {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    }
    
    return response;
}

json generate_ai_summary(const AzureOpenAIConfig& config,
                         const std::string& cord_uid,
                         Engine* engine,
                         StatsTracker* stats,
                         bool is_authorized) {
    json response_json;
    
    // Check cache first if engine is provided
    if (engine) {
        std::string cache_key = "summary|" + cord_uid;
        
        std::lock_guard<std::mutex> lock(engine->mtx);
        json cached = engine->get_ai_from_cache(cache_key);
        
        if (!cached.is_null() && cached.contains("from_cache")) {
            std::cerr << "[ai_summary] Cache HIT for cord_uid: \"" << cord_uid << "\"\n";
            
            // Track cache hit and increment calls (cache hit is still a call)
            if (stats) {
                stats->increment_ai_summary_calls();
                stats->increment_ai_summary_cache_hits();
            }
            
            // Remove internal flag and add user-visible flag
            cached.erase("from_cache");
            cached["cached"] = true;
            return cached;
        }
        
        std::cerr << "[ai_summary] Cache MISS for cord_uid: \"" << cord_uid << "\"\n";
    }
    
    try {
        // Look up metadata for the cord_uid
        if (!engine || engine->uid_to_meta.find(cord_uid) == engine->uid_to_meta.end()) {
            response_json["error"] = "cord_uid not found in metadata";
            response_json["success"] = false;
            response_json["cord_uid"] = cord_uid;
            std::cerr << "[ai_summary] cord_uid not found: " << cord_uid << "\n";
            return response_json;
        }
        
        const auto& meta = engine->uid_to_meta.at(cord_uid);
        
        // Check if abstract exists
        if (meta.abstract.empty()) {
            response_json["error"] = "No abstract available for this document";
            response_json["success"] = false;
            response_json["cord_uid"] = cord_uid;
            std::cerr << "[ai_summary] No abstract for cord_uid: " << cord_uid << "\n";
            return response_json;
        }
        
        // Build the API path
        std::string path = "/openai/deployments/" + config.model + 
                          "/chat/completions?api-version=" + config.api_version;
        
        // Build the request body
        json request_body;
        request_body["messages"] = json::array();
        
        // Add system message
        json system_msg;
        system_msg["role"] = "system";
        system_msg["content"] = build_summary_system_prompt();
        request_body["messages"].push_back(system_msg);
        
        // Add user message with title and abstract
        json user_msg;
        user_msg["role"] = "user";
        user_msg["content"] = build_summary_user_prompt(meta.title, meta.abstract);
        request_body["messages"].push_back(user_msg);
        
        // Set parameters
        request_body["max_completion_tokens"] = 500;
        
        std::string body_str = request_body.dump();
        
        std::cerr << "[azure_openai] Calling Azure OpenAI for summary at " << config.endpoint << path << "\n";
        
        // Decrement AI API calls remaining only for unauthorized requests
        if (stats && !is_authorized) {
            stats->decrement_ai_api_calls();
            std::cerr << "[azure_openai] Unauthorized request - decrementing counter\n";
        } else if (is_authorized) {
            std::cerr << "[azure_openai] Authorized request - counter not decremented\n";
        }
        
        // Make the HTTPS POST request using WinHTTP
        std::string response_body = make_https_post_summary(config.endpoint, path, config.api_key, body_str);
        
        if (response_body.empty()) {
            response_json["error"] = "Failed to connect to Azure OpenAI";
            response_json["success"] = false;
            response_json["cord_uid"] = cord_uid;
            std::cerr << "[azure_openai] Connection failed\n";
            return response_json;
        }
        
        // Parse the response
        json api_response = json::parse(response_body);
        
        // Check for API errors
        if (api_response.contains("error")) {
            response_json["error"] = "Azure OpenAI API error";
            response_json["details"] = api_response["error"];
            response_json["success"] = false;
            response_json["cord_uid"] = cord_uid;
            std::cerr << "[azure_openai] API error: " << api_response.dump() << "\n";
            return response_json;
        }
        
        // Extract the AI summary from the response
        if (api_response.contains("choices") && 
            api_response["choices"].is_array() && 
            !api_response["choices"].empty()) {
            
            const auto& choice = api_response["choices"][0];
            if (choice.contains("message") && 
                choice["message"].contains("content")) {
                
                response_json["success"] = true;
                response_json["cord_uid"] = cord_uid;
                response_json["summary"] = choice["message"]["content"];
                response_json["cached"] = false;
                
                // Only increment ai_summary_calls on successful generation
                if (stats) {
                    stats->increment_ai_summary_calls();
                }
                
                std::cerr << "[azure_openai] Successfully generated AI summary\n";
                
                // Cache the successful response if engine is provided
                if (engine) {
                    std::string cache_key = "summary|" + cord_uid;
                    std::lock_guard<std::mutex> lock(engine->mtx);
                    engine->put_ai_in_cache(cache_key, response_json);
                    std::cerr << "[ai_summary] Cached AI summary for cord_uid: \"" << cord_uid << "\"\n";
                }
            } else {
                response_json["error"] = "Unexpected response structure";
                response_json["success"] = false;
                response_json["cord_uid"] = cord_uid;
            }
        } else {
            response_json["error"] = "No choices in response";
            response_json["success"] = false;
            response_json["cord_uid"] = cord_uid;
        }
        
    } catch (const std::exception& e) {
        response_json["error"] = std::string("Exception: ") + e.what();
        response_json["success"] = false;
        response_json["cord_uid"] = cord_uid;
        std::cerr << "[azure_openai] Exception: " << e.what() << "\n";
    }
    
    return response_json;
}

} // namespace cord19
