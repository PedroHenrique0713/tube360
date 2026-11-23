#include "cast/lounge.hpp"
#include <thread>
#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::string port = argv[1], origin = "https://127.0.0.1:"+port;
    std::atomic_bool cancel{false}; unsigned count = 0;
    auto status = cast::channel(origin+"/frames",cancel,[&](const auto& frame) { ++count; return frame == "[]"; });
    if (status != 200 || count != 2) return 2;
    if (cast::channel("https://localhost:"+port+"/frames",cancel,[](const auto&) { return true; }) != 0) return 3;
    if (cast::channel(origin+"/expired",cancel,[](const auto&) { return true; }) != 400) return 4;
    std::thread stopper([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); cancel = true; });
    auto start = std::chrono::steady_clock::now();
    cast::channel(origin+"/idle",cancel,[](const auto&) { return true; }); stopper.join();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    if (ms > 2500) return 5;
    std::cout << "PASS production TLS stream, fragmented frames, hostname rejection, HTTP 400, cancellation " << ms << " ms\n";
}
