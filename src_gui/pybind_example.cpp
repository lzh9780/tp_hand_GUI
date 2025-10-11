// src/main.cpp
#include <chrono>
#include <iostream>
#include <thread>

#include <pybind11/embed.h>
#include <pybind11/stl.h>   // for dict -> std::map/std::string conversions

namespace py = pybind11;
using namespace std::chrono_literals;

int main() {
    // 1) Start the Python interpreter for the whole process.
    py::scoped_interpreter guard{};

    try {
        // 2) Put ./scripts on sys.path so we can import producer.py
        py::module sys = py::module::import("sys");
        sys.attr("path").cast<py::list>().insert(0, "scripts");

        // 3) Import module and construct the Python class instance
        py::object producer = py::module::import("producer");
        py::object SensorLoop = producer.attr("SensorLoop");
        py::object loop = SensorLoop(py::float_(200.0)); // internal Python loop rate

        // Cache bound callables to reduce attribute lookups per tick
        py::object start_fn = loop.attr("start");
        py::object stop_fn  = loop.attr("stop");
        py::object get_latest_fn = loop.attr("get_latest");

        // 4) Start the Python loop (runs in a Python thread)
        start_fn();

        // 5) Start a C++ reader thread that polls ~100 Hz
        std::atomic<bool> run{true};
        std::thread reader([&](){
            // Each time we touch Python, we must hold the GIL.
            // pybind11 makes this explicit with gil_scoped_acquire.
            while (run.load(std::memory_order_relaxed)) {
                {
                    py::gil_scoped_acquire gil;
                    // get_latest() returns a dict: {"t": float, "value": float, "tick": int}
                    py::dict d = get_latest_fn().cast<py::dict>();
                    double t   = d["t"].cast<double>();
                    double val = d["value"].cast<double>();
                    long tick  = d["tick"].cast<long>();
                    // Do something with the data (e.g., feed controller, log, etc.)
                    std::cout << "tick=" << tick << " t=" << t << " val=" << val << "\n";
                }
                // Release CPU; no GIL held here
                std::this_thread::sleep_for(10ms); // ~100 Hz
            }
        });

        // 6) Run for a few seconds (simulate your program lifetime)
        std::this_thread::sleep_for(2s);

        // 7) Clean shutdown
        run.store(false);
        reader.join();

        {
            py::gil_scoped_acquire gil;
            stop_fn(); // tell Python loop to stop and join its thread
        }
        std::cout << "Shutdown cleanly.\n";
    } catch (const py::error_already_set& e) {
        // If any Python exception propagates, print it.
        std::cerr << "Python error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "C++ error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
