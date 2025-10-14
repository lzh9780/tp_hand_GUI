// src/main.cpp
#include <chrono>
#include <iostream>
#include <thread>

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>   // for dict -> std::map/std::string conversions

# include <opencv2/opencv.hpp>

namespace py = pybind11;
using namespace std::chrono_literals;

struct PyMatView {
    cv::Mat mat;
    py::array arr;

    static PyMatView from_numpy(const py::array& a) {
        PyMatView v;
        v.arr = a;

        py::buffer_info info = v.arr.request();
        if (info.ndim != 3) throw std::runtime_error("Expected HxWxC array");
        if (info.format != py::format_description<uint8_t>::format())
            throw std::runtime_error("Expected dtype=uint8");
        int rows = static_cast<int>(info.shape[0]);
        int cols = static_cast<int>(info.shape[1]);
        int ch   = static_cast<int>(info.shape[2]);
        if (ch != 1 && ch != 3 && ch != 4) throw std::runtime_error("Expected channels 1/3/4");
        if (info.strides[2] != 1) throw std::runtime_error("Array not contiguous in channels");

        int type = (ch == 1) ? CV_8UC1 : (ch == 3 ? CV_8UC3 : CV_8UC4);
        v.mat = cv::Mat(rows, cols, type, info.ptr, static_cast<size_t>(info.strides(0)));
        return v;
    }
}

int main() {
    // 1) Start the Python interpreter for the whole process.
    py::scoped_interpreter guard{};

    try {
        // 2) Put ./scripts on sys.path so we can import producer.py
        py::module sys = py::module::import("sys");
        sys.attr("path").cast<py::list>().insert(0, "scripts");

        // 3) Import module and construct the Python class instance
        py::object producer = py::module::import("hand_detect");
        py::object HnadDetector = producer.attr("HnadDetector");
        py::object loop = HnadDetector(); // internal Python loop rate

        // Cache bound callables to reduce attribute lookups per tick
        py::object start_fn = loop.attr("start");
        py::object stop_fn  = loop.attr("stop");
        py::object get_latest_fn = loop.attr("get_joints");

        // 4) Start the Python loop (runs in a Python thread)
        start_fn();

        // 5) Start a C++ reader thread that polls ~100 Hz
        std::atomic<bool> run{true};
        std::thread reader([&](){
            // Each time we touch Python, we must hold the GIL.
            // pybind11 makes this explicit with gil_scoped_acquire.
            while (run.load(std::memory_order_relaxed)) {
                
                py::gil_scoped_acquire gil;
                // get_latest() returns a dict: {"t": float, "value": float, "tick": int}
                py::tuple tup = get_latest_fn().cast<py::tuple>();
                py::array arr = tup[0].cast<py::array>();
                py::dict d = tup[1].cast<py::dict>();

                PyMatView view = PyMatView::from_numpy(arr);
                cv::Mat img = view.mat;

                cv::imshow("example", img);
                cv::waitKey(1);
                
                // Release CPU; no GIL held here
                gil.~gil_scoped_acquire();
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
