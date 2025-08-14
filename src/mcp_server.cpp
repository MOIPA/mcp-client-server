#include "httplib.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <thread> // for sleep
#include <chrono> // for sleep
#include <vector>

// for convenience
using json = nlohmann::json;
namespace fs = std::filesystem;

// Error handling helper
json create_error_response(const std::string& error_message) {
    json response;
    response["success"] = false;
    response["error"] = error_message;
    return response;
}

// State for our simple SSE streaming. 
// NOTE: static variables are not thread-safe. This is a simplified example for learning.
// For a real multi-user server, you would need a more robust way to manage state per-request.
static std::vector<std::string> sse_entries;
static int sse_current_entry_index;

int main() {
    httplib::Server svr;

    // 1. Endpoint to list directory contents (Standard HTTP Request/Response)
    svr.Post("/list_directory", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        try {
            auto body = json::parse(req.body);
            std::string path_str = body.at("path");

            if (!fs::exists(path_str)) {
                res.status = 404;
                res.set_content(create_error_response("Path does not exist.").dump(4), "application/json");
                return;
            }

            if (!fs::is_directory(path_str)) {
                res.status = 400;
                res.set_content(create_error_response("Path is not a directory.").dump(4), "application/json");
                return;
            }

            json file_list = json::array();
            for (const auto& entry : fs::directory_iterator(path_str)) {
                file_list.push_back(entry.path().filename().string());
            }

            json response;
            response["success"] = true;
            response["path"] = path_str;
            response["contents"] = file_list;
            res.set_content(response.dump(4), "application/json");

        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(create_error_response(e.what()).dump(4), "application/json");
        }
    });

    // 2. Endpoint to create a directory
    svr.Post("/create_directory", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        try {
            auto body = json::parse(req.body);
            std::string path_str = body.at("path");

            if (fs::exists(path_str)) {
                res.status = 409; // Conflict
                res.set_content(create_error_response("Path already exists.").dump(4), "application/json");
                return;
            }

            if (fs::create_directories(path_str)) {
                json response;
                response["success"] = true;
                response["message"] = "Directory created successfully at '" + path_str + "'.";
                res.set_content(response.dump(4), "application/json");
            } else {
                res.status = 500;
                res.set_content(create_error_response("Failed to create directory.").dump(4), "application/json");
            }

        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(create_error_response(e.what()).dump(4), "application/json");
        }
    });

    // 3. Endpoint to delete a directory or file
    svr.Post("/delete", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        try {
            auto body = json::parse(req.body);
            std::string path_str = body.at("path");

            if (!fs::exists(path_str)) {
                res.status = 404;
                res.set_content(create_error_response("Path does not exist.").dump(4), "application/json");
                return;
            }

            fs::path canonical_path = fs::weakly_canonical(fs::absolute(path_str));
            fs::path base_path = fs::current_path();
            if (std::distance(base_path.begin(), base_path.end()) > std::distance(canonical_path.begin(), canonical_path.end()) ||
                !std::equal(base_path.begin(), base_path.end(), canonical_path.begin())) {
                 res.status = 403;
                 res.set_content(create_error_response("For security, deletion is restricted to subdirectories of the current working directory.").dump(4), "application/json");
                 return;
            }

            fs::remove_all(path_str);

            json response;
            response["success"] = true;
            response["message"] = "Successfully deleted '" + path_str + "'.";
            res.set_content(response.dump(4), "application/json");

        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(create_error_response(e.what()).dump(4), "application/json");
        }
    });

    // 4. Endpoint for streaming directory contents using SSE (Server-Sent Events)
    svr.Get("/list_directory_stream", [](const httplib::Request& req, httplib::Response& res) {
        // Get path from query param, e.g., /list_directory_stream?path=./
        std::string path_str = req.has_param("path") ? req.get_param_value("path") : ".";

        res.set_chunked_content_provider(
            "text/event-stream",
            [path_str](size_t offset, httplib::DataSink &sink) {
                if (offset == 0) { // First call for this request, so we prepare the data.
                    sse_entries.clear();
                    sse_current_entry_index = 0;
                    try {
                        if (!fs::exists(path_str) || !fs::is_directory(path_str)) {
                            return false; // Stop. 
                        }
                        for (const auto& entry : fs::directory_iterator(path_str)) {
                            json data;
                            data["filename"] = entry.path().filename().string();
                            data["is_directory"] = entry.is_directory();
                            sse_entries.push_back(data.dump());
                        }
                    } catch (...) {
                        return false; // Stop on any exception.
                    }
                }

                // If we have an entry to send, send it.
                if (sse_current_entry_index < sse_entries.size()) {
                    std::stringstream ss;
                    ss << "id: " << sse_current_entry_index + 1 << "\n";
                    ss << "event: file_entry\n";
                    ss << "data: " << sse_entries[sse_current_entry_index] << "\n\n";

                    std::string event_string = ss.str();
                    sink.write(event_string.c_str(), event_string.length());

                    sse_current_entry_index++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    return true; // We might have more data, so we return true.
                } else {
                    // No more entries to send, send the end-of-stream event and stop.
                    std::string end_msg = "event: end_of_stream\ndata: {}\n\n";
                    sink.write(end_msg.c_str(), end_msg.length());
                    return false; // End of stream.
                }
            });
    });

    int port = 8081;
    std::cout << "MCP server starting on http://localhost:" << port << std::endl;
    svr.listen("0.0.0.0", port);

    return 0;
}