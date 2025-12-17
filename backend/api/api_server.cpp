#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "api_add_document.hpp"
#include "api_engine.hpp"
#include "api_http.hpp"
#include "third_party/httplib.h"

using cord19::Engine;
using cord19::json;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: api_server <INDEX_DIR> [port]\n"
                  << "Example: api_server D:\\cord19_index 8080\n";
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

    httplib::Server svr;

    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        cord19::enable_cors(res);
        res.status = 204;
    });

    svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        cord19::enable_cors(res);
        json j;
        j["ok"] = true;
        j["segments"] = (int)engine.segments.size();
        res.set_content(j.dump(2), "application/json");
    });

    svr.Get("/search", [&](const httplib::Request& req, httplib::Response& res) {
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

        double search_ms = std::chrono::duration<double, std::milli>(search_t1 - search_t0).count();
        j["search_time_ms"] = search_ms;

        auto total_t1 = clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(total_t1 - total_t0).count();
        j["total_time_ms"] = total_ms;

        std::cerr << "[search] q=\"" << q << "\" k=" << k << " search=" << search_ms << "ms total="
                  << total_ms << "ms\n";

        res.set_content(j.dump(2), "application/json");
    });

    svr.Post("/add_document", [&](const httplib::Request& req, httplib::Response& res) {
        cord19::handle_add_document(engine, req, res);
    });

    svr.Post("/reload", [&](const httplib::Request&, httplib::Response& res) {
        cord19::enable_cors(res);
        bool ok = engine.reload();
        json j;
        j["reloaded"] = ok;
        j["segments"] = (int)engine.segments.size();
        res.set_content(j.dump(2), "application/json");
    });

    std::cout << "API running on http://127.0.0.1:" << port << "\n";
    std::cout << "Try: /search?q=mycoplasma+pneumonia&k=10\n";
    svr.listen("0.0.0.0", port);
    return 0;
}
