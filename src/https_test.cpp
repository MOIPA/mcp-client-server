#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib_old.h"
#include <iostream>
const std::string PROXY_HOST = "proxyhk.zte.com.cn";
const int PROXY_PORT = 80;
int main() {
    httplib::Client cli("https://web-mcp.koyeb.app");
    cli.set_proxy(PROXY_HOST.c_str(), PROXY_PORT);
    cli.enable_server_certificate_verification(false);
    cli.set_read_timeout(0); // Set to 0 for no timeout, useful for SSE
    cli.set_logger([](const httplib::Request &req, const httplib::Response &res) {
        std::cout << "[Logger] Request: " << req.method << " " << req.path << std::endl;
        std::cout << "[Logger] Response Status: " << res.status << std::endl;
        // NOTE: We can't get the detailed error enum from the Response object itself.
        // The error code is on the Result object returned by Get/Post.
    });

    // Explicitly disable certificate verification.
    cli.enable_server_certificate_verification(false);

    std::cout << "[Test Program] Attempting to connect to SSE endpoint..." << std::endl;
    auto res = cli.Get("/sse/e86f79f2-f0c4-40e1-ae33-b701bb3959a7");

    if (res) {
        std::cout << "\n[Test Program] Connection SUCCEEDED!" << std::endl;
        std::cout << "[Test Program] HTTP Status: " << res->status << std::endl;
        std::cout << "[Test Program] Body snippet: " << res->body.substr(0, 80) << "..." << std::endl;
    } else {
        std::cout << "\n[Test Program] Connection FAILED!" << std::endl;
        // In older versions, the error is in the .err member of the Result object
        auto err = res.error();
        std::cout << "[Test Program] Error code: " << httplib::to_string(err) << std::endl;
    }

    return 0;
}