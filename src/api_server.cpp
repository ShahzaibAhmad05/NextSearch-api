#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "api_add_document.hpp"
#include "api_admin.hpp"
#include "api_ai_overview.hpp"
#include "api_ai_summary.hpp"
#include "api_engine.hpp"
#include "api_feedback.hpp"
#include "api_http.hpp"
#include "api_stats.hpp"
#include "env_loader.hpp"
#include "third_party/httplib.h"

using cord19::Engine;
using cord19::json;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: api_server <INDEX_DIR> [port]\n"
                  << "Example: api_server ./index 8080\n";
        return 1;
    }

    Engine engine;
    engine.index_dir = std::filesystem::path(argv[1]);

    int port = 8080;
    if (argc >= 3) port = std::stoi(argv[2]);

    if (!engine.reload()) {
        std::cerr << "Failed to load index segments from: " << engine.index_dir << "\n";
        return 1;
    }

    // Load Azure OpenAI configuration from .env file
    auto env_vars = cord19::load_env_file(".env");
    cord19::AzureOpenAIConfig azure_config;
    azure_config.endpoint = env_vars["AZURE_OPENAI_ENDPOINT"];
    azure_config.api_key = env_vars["AZURE_OPENAI_API_KEY"];
    azure_config.model = env_vars["AZURE_OPENAI_MODEL"];
    
    // Initialize stats tracker
    cord19::StatsTracker stats_tracker;
    
    // Load AI API limit from .env (default: 10,000)
    if (!env_vars["AI_API_CALLS_LIMIT"].empty()) {
        int64_t limit = std::stoll(env_vars["AI_API_CALLS_LIMIT"]);
        stats_tracker.set_ai_api_calls_limit(limit);
        std::cout << "[stats] AI API calls limit set to: " << limit << "\n";
    } else {
        std::cout << "[stats] AI API calls limit: 10,000 (default)\n";
    }
    
    // Load admin configuration
    std::string admin_password = env_vars["ADMIN_PASSWORD"];
    std::string jwt_secret = env_vars["JWT_SECRET"];
    int jwt_expiration = 3600; // Default 1 hour
    if (!env_vars["JWT_EXPIRATION"].empty()) {
        jwt_expiration = std::stoi(env_vars["JWT_EXPIRATION"]);
    }
    
    // Validate admin configuration
    bool admin_enabled = !admin_password.empty() && !jwt_secret.empty();
    if (!admin_enabled) {
        std::cerr << "[warning] Admin authentication not configured. Set ADMIN_PASSWORD and JWT_SECRET in .env file to enable protected endpoints.\n";
    } else {
        std::cout << "[admin] Admin authentication enabled with JWT expiration: " << jwt_expiration << "s\n";
    }
    
    // Validate Azure configuration
    bool azure_enabled = !azure_config.endpoint.empty() && 
                        !azure_config.api_key.empty() && 
                        !azure_config.model.empty();
    
    if (azure_enabled) {
        std::cout << "[azure] Azure OpenAI enabled with model: " << azure_config.model << "\n";
    } else {
        std::cout << "[azure] Azure OpenAI not configured (AI overview endpoint will return error)\n";
    }

    // Initialize feedback manager with storage in root directory
    cord19::FeedbackManager feedback_manager("feedback.json");

    httplib::Server svr;

    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        std::cerr << "[http] " << req.method << " " << req.path << " -> " << res.status << "\n";
    });

    svr.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            std::cerr << "[exception] " << req.method << " " << req.path << " : " << e.what() << "\n";
        }
        res.status = 500;
        res.set_content(R"({"error":"internal server error"})", "application/json");
    });

    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        std::cerr << "[error] " << req.method << " " << req.path << " -> " << res.status << "\n";
    });

    // CORS preflight handler (OPTIONS) for all routes
    svr.Options(R"(.*)", [](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);

        if (req.has_header("Access-Control-Request-Headers")) {
            res.set_header("Access-Control-Allow-Headers",
                           req.get_header_value("Access-Control-Request-Headers"));
        }

        if (req.has_header("Access-Control-Request-Method")) {
            res.set_header("Access-Control-Allow-Methods",
                           req.get_header_value("Access-Control-Request-Method") + std::string(", OPTIONS"));
        }

        res.status = 204;
    });

    // ---- API routes only ----

    // Admin authentication endpoints
    svr.Post("/api/admin/login", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);
        
        if (!admin_enabled) {
            res.status = 503;
            json err;
            err["error"] = "Admin authentication not configured";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Parse request body
        json req_body;
        try {
            req_body = json::parse(req.body);
        } catch (const std::exception& e) {
            res.status = 400;
            json err;
            err["error"] = "Invalid JSON request body";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Check for password field
        if (!req_body.contains("password")) {
            res.status = 400;
            json err;
            err["error"] = "Password is required";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        std::string password = req_body["password"];
        
        // Validate password
        if (password != admin_password) {
            res.status = 401;
            json err;
            err["error"] = "Invalid admin password";
            res.set_content(err.dump(2), "application/json");
            std::cerr << "[admin] Failed login attempt\n";
            return;
        }
        
        // Generate JWT token
        std::string token = cord19::generate_jwt_token(jwt_secret, jwt_expiration);
        
        json response;
        response["token"] = token;
        response["expires_in"] = jwt_expiration;
        
        res.set_content(response.dump(2), "application/json");
        std::cerr << "[admin] Successful login, token issued\n";
    });

    svr.Post("/api/admin/logout", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);
        
        // Simple logout - frontend will clear token
        // Optional: implement token blacklisting here
        json response;
        response["message"] = "Logged out successfully";
        res.set_content(response.dump(2), "application/json");
    });

    svr.Get("/api/admin/verify", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);
        
        if (!admin_enabled) {
            res.status = 401;
            json err;
            err["valid"] = false;
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Get Authorization header
        if (!req.has_header("Authorization")) {
            res.status = 401;
            json err;
            err["valid"] = false;
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        std::string auth_header = req.get_header_value("Authorization");
        std::string token = cord19::extract_bearer_token(auth_header);
        
        if (token.empty()) {
            res.status = 401;
            json err;
            err["valid"] = false;
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Validate token
        auto validation_result = cord19::validate_jwt_token(token, jwt_secret);
        
        if (!validation_result.valid) {
            res.status = 401;
            json err;
            err["valid"] = false;
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Token is valid - return expiration time
        json response;
        response["valid"] = true;
        int64_t exp = validation_result.payload["exp"];
        response["expires_at"] = exp * 1000; // Convert to milliseconds
        
        res.set_content(response.dump(2), "application/json");
    });

    svr.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
        cord19::enable_cors(res);
        json j;
        j["ok"] = true;
        j["segments"] = (int)engine.segments.size();
        res.set_content(j.dump(2), "application/json");
    });

    svr.Get("/api/search", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);

        using clock = std::chrono::steady_clock;
        auto total_t0 = clock::now();

        if (!req.has_param("q")) {
            res.status = 400;
            res.set_content(R"({"error":"missing q param"})", "application/json");
            return;
        }

        std::string q = req.get_param_value("q");
        int k = 10;
        if (req.has_param("k")) k = std::stoi(req.get_param_value("k"));

        auto search_t0 = clock::now();
        auto j = engine.search(q, k);
        auto search_t1 = clock::now();

        double search_ms =
            std::chrono::duration<double, std::milli>(search_t1 - search_t0).count();
        
        // Check if result was from cache
        bool from_cache = j.contains("from_cache") && j["from_cache"] == true;
        
        // Track search stats
        stats_tracker.increment_searches();
        if (from_cache) {
            stats_tracker.increment_search_cache_hits();
        }
        
        if (from_cache) {
            // For cached results: search_time_ms = 0, cache lookup time added to total
            j["search_time_ms"] = 0.0;
            j["cache_lookup_ms"] = search_ms;
            
            auto total_t1 = clock::now();
            double total_ms =
                std::chrono::duration<double, std::milli>(total_t1 - total_t0).count();
            j["total_time_ms"] = total_ms;
            j["cached"] = true;
            j.erase("from_cache"); // Remove internal flag
            
            std::cerr << "[search] q=\"" << q << "\" k=" << k
                      << " CACHED cache_lookup=" << search_ms << "ms total=" << total_ms << "ms\n";
        } else {
            // For new searches: set search time and total time
            j["search_time_ms"] = search_ms;
            
            auto total_t1 = clock::now();
            double total_ms =
                std::chrono::duration<double, std::milli>(total_t1 - total_t0).count();
            j["total_time_ms"] = total_ms;
            j["cached"] = false;
            
            std::cerr << "[search] q=\"" << q << "\" k=" << k
                      << " search=" << search_ms << "ms total=" << total_ms << "ms\n";
        }

        res.set_content(j.dump(2), "application/json");
    });

    svr.Get("/api/suggest", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);

        if (!req.has_param("q")) {
            res.status = 400;
            res.set_content(R"({"error":"missing q param"})", "application/json");
            return;
        }

        std::string q = req.get_param_value("q");
        int k = 5;
        if (req.has_param("k")) k = std::stoi(req.get_param_value("k"));

        auto j = engine.suggest(q, k);
        res.set_content(j.dump(2), "application/json");
    });

    svr.Post("/api/add_document",
             [&](const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& cr) {
                 cord19::enable_cors(res);
                 
                 // Require admin authentication
                 if (admin_enabled && !cord19::require_admin_auth(req, res, jwt_secret)) {
                     return; // Error response already set by require_admin_auth
                 }
                 
                 cord19::handle_add_document(engine, req, res, cr);
             });

    svr.Post("/api/reload", [&](const httplib::Request&, httplib::Response& res) {
        cord19::enable_cors(res);
        bool ok = engine.reload();
        json j;
        j["reloaded"] = ok;
        j["segments"] = (int)engine.segments.size();
        res.set_content(j.dump(2), "application/json");
    });

    svr.Get("/api/ai_overview", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);
        
        // Require admin authentication
        if (admin_enabled && !cord19::require_admin_auth(req, res, jwt_secret)) {
            return; // Error response already set by require_admin_auth
        }
        
        // Check if Azure OpenAI is configured
        if (!azure_enabled) {
            res.status = 503;
            json err;
            err["error"] = "Azure OpenAI not configured. Please set AZURE_OPENAI_ENDPOINT, AZURE_OPENAI_API_KEY, and AZURE_OPENAI_MODEL in .env file";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Extract query parameter from URL
        if (!req.has_param("q")) {
            res.status = 400;
            json err;
            err["error"] = "missing q param";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        std::string query = req.get_param_value("q");
        
        // Get k parameter (default to 10)
        int k = 10;
        if (req.has_param("k")) {
            k = std::stoi(req.get_param_value("k"));
        }
        
        std::cerr << "[ai_overview] Processing query: \"" << query << "\" k=" << k << "\n";
        
        // Wait for cached results (retry with backoff for race condition with parallel /api/search call)
        json search_results;
        bool found_cache = false;
        const int max_retries = 10;  // Max ~500ms wait (10 * 50ms)
        
        for (int retry = 0; retry < max_retries; retry++) {
            search_results = engine.search(query, k);
            
            // Check if results came from cache (meaning /api/search already populated it)
            if (search_results.contains("from_cache") && search_results["from_cache"] == true) {
                found_cache = true;
                std::cerr << "[ai_overview] Found cached results after " << retry << " retries\n";
                break;
            }
            
            // If we have results (even if not cached yet), we can use them
            if (search_results.contains("results") && !search_results["results"].empty()) {
                std::cerr << "[ai_overview] Using fresh search results (cache being populated)\n";
                found_cache = true;
                break;
            }
            
            // Wait 50ms before retry (allow time for parallel /api/search to populate cache)
            if (retry < max_retries - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        // Check if we got valid results
        if (!search_results.contains("results") || search_results["results"].empty()) {
            res.status = 404;
            json err;
            err["error"] = "No search results found for the query";
            err["query"] = query;
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Generate AI overview using Azure OpenAI with caching
        auto ai_response = cord19::generate_ai_overview(azure_config, query, k, search_results, &engine, &stats_tracker);
        
        // Prepare minimal response (no search results, just AI overview)
        json response;
        response["query"] = query;
        
        if (ai_response.contains("success") && ai_response["success"] == true) {
            response["overview"] = ai_response["overview"];
            response["model"] = ai_response["model"];
            if (ai_response.contains("usage")) {
                response["usage"] = ai_response["usage"];
            }
            res.set_content(response.dump(2), "application/json");
        } else {
            res.status = 500;
            response["error"] = ai_response.contains("error") ? ai_response["error"] : "Unknown error";
            if (ai_response.contains("details")) {
                response["details"] = ai_response["details"];
            }
            res.set_content(response.dump(2), "application/json");
        }
    });

    svr.Get("/api/ai_summary", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);
        
        // Require admin authentication
        if (admin_enabled && !cord19::require_admin_auth(req, res, jwt_secret)) {
            return; // Error response already set by require_admin_auth
        }
        
        // Check if Azure OpenAI is configured
        if (!azure_enabled) {
            res.status = 503;
            json err;
            err["error"] = "Azure OpenAI not configured. Please set AZURE_OPENAI_ENDPOINT, AZURE_OPENAI_API_KEY, and AZURE_OPENAI_MODEL in .env file";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        // Extract cord_uid parameter from URL
        if (!req.has_param("cord_uid")) {
            res.status = 400;
            json err;
            err["error"] = "missing cord_uid param";
            res.set_content(err.dump(2), "application/json");
            return;
        }
        
        std::string cord_uid = req.get_param_value("cord_uid");
        
        std::cerr << "[ai_summary] Processing cord_uid: \"" << cord_uid << "\"\n";
        
        // Generate AI summary using Azure OpenAI with caching
        auto ai_response = cord19::generate_ai_summary(azure_config, cord_uid, &engine, &stats_tracker);
        
        // Return the response (contains only cord_uid and summary)
        if (ai_response.contains("success") && ai_response["success"] == true) {
            json response;
            response["cord_uid"] = ai_response["cord_uid"];
            response["summary"] = ai_response["summary"];
            if (ai_response.contains("cached")) {
                response["cached"] = ai_response["cached"];
            }
            res.set_content(response.dump(2), "application/json");
        } else {
            res.status = ai_response.contains("cord_uid") ? 404 : 500;
            json error_response;
            error_response["cord_uid"] = cord_uid;
            error_response["error"] = ai_response.contains("error") ? ai_response["error"] : "Unknown error";
            if (ai_response.contains("details")) {
                error_response["details"] = ai_response["details"];
            }
            res.set_content(error_response.dump(2), "application/json");
        }
    });

    svr.Post("/api/feedback", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::handle_feedback(feedback_manager, req, res);
    });

    svr.Get("/api/stats", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::enable_cors(res);
        
        // Require admin authentication
        if (admin_enabled && !cord19::require_admin_auth(req, res, jwt_secret)) {
            return; // Error response already set by require_admin_auth
        }
        
        // Get comprehensive stats from tracker
        json stats = stats_tracker.get_stats_json(feedback_manager);
        
        res.set_content(stats.dump(2), "application/json");
    });

    std::cout << "API running on http://127.0.0.1:" << port << "\n";
    std::cout << "Try: /api/search?q=mycoplasma+pneumonia&k=10\n";
    if (azure_enabled) {
        std::cout << "Try: /api/ai_overview?q=covid&k=10\n";
        std::cout << "Try: /api/ai_summary?cord_uid=<some_uid>\n";
    }
    svr.listen("0.0.0.0", port);
    return 0;
}
