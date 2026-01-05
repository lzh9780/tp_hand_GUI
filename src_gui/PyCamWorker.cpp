#include "PyCamWorker.h"
#include <QDebug>
// #include <opencv2/imgproc.hpp> // for color conversion if needed (we’ll do manual)
#include <stdexcept>
#include <QList>

// using namespace Qt::Literals::StringLiterals;

PyCamWorker::PyCamWorker(QObject* parent) : QObject(parent) {}

PyCamWorker::~PyCamWorker() { stop(); }

void PyCamWorker::start() {
    if (running_) return;

    try {
        // Start the Python interpreter (scoped for this worker thread)
        // interp_ = std::make_unique<py::scoped_interpreter>();

        // Ensure ./scripts on sys.path
        {
            py::gil_scoped_acquire gil;
            py::module sys = py::module::import("sys");
            sys.attr("path").cast<py::list>().insert(0, "scripts");

            // 3) Import module and construct the Python class instance
            py::object producer = py::module::import("hand_detect");
            py::object HandDetector = producer.attr("HandDetector");
            py::object loop = HandDetector(); // internal Python loop rate

            // Cache bound callables to reduce attribute lookups per tick
            py::object start_fn = loop.attr("start");
            stop_fn_  = loop.attr("stop");
            get_latest_fn = loop.attr("get_frame");
            get_joint_fn = loop.attr("get_joints");
            get_wrist_joint_fn = loop.attr("get_wrist_joints");

            start_fn();
        }
        
        // Poll ~60 FPS (adjust as needed)
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &PyCamWorker::onTick);
        timer_->start(1000/120);

        running_ = true;
        emit status("Python camera loop started.");
        emit runningChanged(running_);
    } catch (const py::error_already_set& e) {
        emit error(QString::fromUtf8(e.what()));
    } catch (const std::exception& e) {
        emit error(QString::fromUtf8(e.what()));
    }
}

void PyCamWorker::stop() {
    if (!running_) return;
    running_ = false;
    if (timer_) { timer_->stop(); timer_->deleteLater(); timer_ = nullptr; }
    try {
        if (stop_fn_) {
            py::gil_scoped_acquire gil;
            stop_fn_();
        }
    } catch (const py::error_already_set& e) {
        // report but continue shutdown
        emit error(QString::fromUtf8(e.what()));
    }
    {
        py::gil_scoped_acquire gil;
        stop_fn_ = py::object();
        get_latest_fn = py::object();
        get_joint_fn = py::object();
    }
    // interp_.reset();
    emit status("Python camera loop stopped.");
    emit runningChanged(running_);
}

static inline void copy_bgr_rows(void* dst, int dstStride, const void* src, int srcStride, int rows, int rowBytes) {
    auto* d = static_cast<char*>(dst);
    auto* s = static_cast<const char*>(src);
    for (int r = 0; r < rows; ++r) {
        std::memcpy(d + r*dstStride, s + r*srcStride, rowBytes);
    }
}

void PyCamWorker::onTick() {
    if (!running_) return;
    // qDebug() << "on tick";
    try {
        py::gil_scoped_acquire gil;
        // get_latest() -> (numpy array HxWx3 uint8 BGR, tick)
        // py::tuple tup = get_latest_fn().cast<py::tuple>();
        py::array arr = get_latest_fn().cast<py::tuple>();// tup[0].cast<py::array>();
        py::dict d = get_joint_fn().cast<py::dict>();
        py::dict wd = get_wrist_joint_fn().cast<py::dict>();

        // Data transform: finger_joint_Node -> finger_joint
        float thumb_joint = 180 - (d["thumb"]["ip"].cast<float>()) + 180 - d["thumb"]["mcp"].cast<float>();
        float index_joint = 180 - d["index"]["pip"].cast<float>() + 180 - d["index"]["mcp"].cast<float>();
        float middle_joint = 180 - d["middle"]["pip"].cast<float>() + 180 - d["middle"]["mcp"].cast<float>();
        float ring_joint = 180 - d["ring"]["pip"].cast<float>() + 180 - d["ring"]["mcp"].cast<float>();
        float pinky_joint = 180 - d["pinky"]["pip"].cast<float>() + 180 - d["pinky"]["mcp"].cast<float>();
        float pitch = wd["pitch"].cast<float>();
        float yaw = wd["yaw"].cast<float>();

        // Data processing: bias
        thumb_joint += -12;
        index_joint += -25;
        middle_joint += -40;
        ring_joint += -28;
        pinky_joint += -40;

        // Data processing: scale
        thumb_joint = thumb_joint*1.8;
        index_joint = index_joint*1.0;
        middle_joint = middle_joint*1.0;
        ring_joint = ring_joint*1.0;
        pinky_joint = pinky_joint*1.4;

        QVector<double> l;
        l.append(thumb_joint);
        l.append(index_joint);
        l.append(middle_joint);
        l.append(ring_joint);
        l.append(pinky_joint);
        l.append(pitch);
        l.append(yaw);
        emit newJoint(l);

        // PyMatView view = PyMatView::from_numpy(arr);
        // cv::Mat cv_img = view.mat;

        py::buffer_info info = arr.request();
        // if (info.ndim != 3) return;
        // if (info.format != py::format_descriptor<uint8_t>::format()) return;
        // int rows = int(info.shape[0]);
        // int cols = int(info.shape[1]);
        // int ch   = int(info.shape[2]);
        // if (ch != 3) return;

        // // info.strides[0] is row stride (bytes), [2] should be 1 (contiguous channels)
        // if (info.strides[2] != 1) return;
        if (info.ndim == 3 &&
            info.format == py::format_descriptor<uint8_t>::format() &&
            int(info.shape[2]) == 3)
        {
            const int rows = int(info.shape[0]);
            const int cols = int(info.shape[1]);
            const int srcStride = int(info.strides[0]);     // bytes per row from numpy
            const int rowBytes  = cols * 3;                 // BGR888

            // 2) Resize our QImage once if size changed
            if (frameBGR_.width() != cols || frameBGR_.height() != rows) {
                frameBGR_ = QImage(cols, rows, QImage::Format_BGR888);
            }

            // 3) memcpy — single pass, no per-pixel work
            copy_bgr_rows(frameBGR_.bits(), frameBGR_.bytesPerLine(),
                          info.ptr, srcStride, rows, rowBytes);

            // 4) Emit to GUI (no pixmap conversion; viewer draws QImage)
            emit newFrame(frameBGR_);
        }

        // Convert to QImage (deep copy so GUI owns pixels safely)
        // QImage img = bgrMatToQImage(static_cast<uint8_t*>(info.ptr), rows, cols, int(info.strides[0]));
        // qDebug() << "Image returned";

        // emit newFrame(img);
    } catch (const py::error_already_set& e) {
        emit error(QString::fromUtf8(e.what()));
    } catch (const std::exception& e) {
        emit error(QString::fromUtf8(e.what()));
    }
}
