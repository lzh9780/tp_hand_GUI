#include "TeleOperation.hpp"

UDETeleop::UDETeleop(QObject *parent) : TeleopBase(parent)
{
    sdk = new UDEGloveSDK();
    sdk->Initialize();
}

void UDETeleop::start()
{
    timer_ = new QTimer();
    connect(timer_, &QTimer::timeout, this, &UDETeleop::onTick);
    timer_->start(1000 / 120);

    sdk->StartListening();
    cout << "Server Start Listening.." << endl;

    running_ = true;
    emit status("Glove receive loop started.");
    emit runningChanged(running_);
}

void UDETeleop::stop()
{
    if (!running_)
        return;
    running_ = false;
    if (timer_)
    {
        timer_->stop();
        timer_->deleteLater();
        timer_ = nullptr;
    }
    try
    {
        if (sdk->GetStatus() != ServerStatus::END)
            sdk->EndListening();
        delete sdk;
        sdk = nullptr;
    }
    catch (const std::exception &e)
    {
        // report but continue shutdown
        emit error(QString::fromUtf8(e.what()));
    }
    emit status("Glove receive loop stopped.");
    emit runningChanged(running_);
}

void UDETeleop::onTick()
{
    if (!running_)
        return;

    Json::Value::Members list;
    try
    {
        list = sdk->GetRoleNameList();
        if (list.size() > 0)
        {
            auto FingerData = sdk->GetVecFingerData(list[0]);
            auto ControllerMapData = sdk->GetVecControllerData(list[0]);
            if (sdk->GetStatus() != 2)
                return;

            QVector<double> l;
            l.append(-FingerData[0].y);
            l.append(-FingerData[0].x);
            l.append(-FingerData[1].x);
            l.append(-FingerData[2].x);
            l.append(-FingerData[3].x);
            l.append(-FingerData[4].x);
            l.append(-FingerData[5].x);
            l.append(-FingerData[3].y);
            l.append(-FingerData[6].x);
            l.append(-FingerData[7].x);
            l.append(-FingerData[8].x);
            l.append(-FingerData[6].y);
            l.append(-FingerData[9].x);
            l.append(-FingerData[10].x);
            l.append(-FingerData[11].x);
            l.append(-FingerData[9].y);
            l.append(-FingerData[12].x);
            l.append(-FingerData[13].x);
            l.append(-FingerData[14].x);
            l.append(-FingerData[12].y);
            l.append(0);
            l.append(0);
            l.append(0);
            l.append(0);

            emit newJoint(l);

            list.clear();
        }
    }
    catch (const std::exception &e)
    {
        // std::cerr << e.what() << '\n';
        emit error(QString::fromUtf8(e.what()));
    }
}

PyCamWorker::PyCamWorker(QObject *parent) : TeleopBase(parent) {}

void PyCamWorker::start()
{
    if (running_)
        return;

    try
    {
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
            stop_fn_ = loop.attr("stop");
            get_latest_fn = loop.attr("get_frame");
            get_joint_fn = loop.attr("get_joints");
            get_wrist_joint_fn = loop.attr("get_wrist_joints");

            start_fn();
        }

        // Poll ~60 FPS (adjust as needed)
        timer_ = new QTimer();
        connect(timer_, &QTimer::timeout, this, &PyCamWorker::onTick);
        timer_->start(1000 / 120);

        running_ = true;
        emit status("Python camera loop started.");
        emit runningChanged(running_);
    }
    catch (const py::error_already_set &e)
    {
        emit error(QString::fromUtf8(e.what()));
    }
    catch (const std::exception &e)
    {
        emit error(QString::fromUtf8(e.what()));
    }
}

void PyCamWorker::stop()
{
    if (!running_)
        return;
    running_ = false;
    if (timer_)
    {
        timer_->stop();
        timer_->deleteLater();
        timer_ = nullptr;
    }
    try
    {
        if (stop_fn_)
        {
            py::gil_scoped_acquire gil;
            stop_fn_();
        }
    }
    catch (const py::error_already_set &e)
    {
        // report but continue shutdown
        emit error(QString::fromUtf8(e.what()));
    }
    {
        py::gil_scoped_acquire gil;
        stop_fn_ = py::object();
        get_latest_fn = py::object();
        get_joint_fn = py::object();
        get_wrist_joint_fn = py::object();
        start_fn = py::object();
    }
    emit status("Python camera loop stopped.");
    emit runningChanged(running_);
}

static inline void copy_bgr_rows(void *dst, int dstStride, const void *src, int srcStride, int rows, int rowBytes)
{
    auto *d = static_cast<char *>(dst);
    auto *s = static_cast<const char *>(src);
    for (int r = 0; r < rows; ++r)
    {
        std::memcpy(d + r * dstStride, s + r * srcStride, rowBytes);
    }
}

void PyCamWorker::onTick()
{
    if (!running_)
        return;
    // qDebug() << "on tick";
    try
    {
        py::gil_scoped_acquire gil;
        // get_latest() -> (numpy array HxWx3 uint8 BGR, tick)
        py::array arr = get_latest_fn().cast<py::tuple>(); // tup[0].cast<py::array>();
        py::dict d = get_joint_fn().cast<py::dict>();
        py::dict wd = get_wrist_joint_fn().cast<py::dict>();

        // Data transform: finger_joint_Node -> finger_joint
        QVector<double> l;
        l.append(d["thumb"]["cmc_pull"].cast<float>());
        l.append(d["thumb"]["cmc_rot"].cast<float>());
        l.append(float(180) - d["thumb"]["mcp"].cast<float>());
        l.append(float(180) - d["thumb"]["ip"].cast<float>());
        l.append(float(180) - d["index"]["mcp"].cast<float>());
        l.append(float(180) - d["index"]["pip"].cast<float>());
        l.append(float(180) - d["index"]["dip"].cast<float>());
        l.append(d["index"]["sway"].cast<float>());
        l.append(float(180) - d["middle"]["mcp"].cast<float>());
        l.append(float(180) - d["middle"]["pip"].cast<float>());
        l.append(float(180) - d["middle"]["dip"].cast<float>());
        l.append(d["middle"]["sway"].cast<float>());
        l.append(float(180) - d["ring"]["mcp"].cast<float>());
        l.append(float(180) - d["ring"]["pip"].cast<float>());
        l.append(float(180) - d["ring"]["dip"].cast<float>());
        l.append(d["ring"]["sway"].cast<float>());
        l.append(float(180) - d["pinky"]["mcp"].cast<float>());
        l.append(float(180) - d["pinky"]["pip"].cast<float>());
        l.append(float(180) - d["pinky"]["dip"].cast<float>());
        l.append(d["pinky"]["sway"].cast<float>());
        l.append(float(180) - d["pinky"]["roll"].cast<float>());
        l.append(wd["roll"].cast<float>());
        l.append(wd["pitch"].cast<float>());
        l.append(wd["yaw"].cast<float>());

        emit newJoint(l);

        py::buffer_info info = arr.request();

        // info.strides[0] is row stride (bytes), [2] should be 1 (contiguous channels)
        // if (info.strides[2] != 1) return;
        if (info.ndim == 3 &&
            info.format == py::format_descriptor<uint8_t>::format() &&
            int(info.shape[2]) == 3)
        {
            const int rows = int(info.shape[0]);
            const int cols = int(info.shape[1]);
            const int srcStride = int(info.strides[0]); // bytes per row from numpy
            const int rowBytes = cols * 3;              // BGR888

            // 2) Resize our QImage once if size changed
            if (frameBGR_.width() != cols || frameBGR_.height() != rows)
            {
                frameBGR_ = QImage(cols, rows, QImage::Format_BGR888);
            }

            // 3) memcpy — single pass, no per-pixel work
            copy_bgr_rows(frameBGR_.bits(), frameBGR_.bytesPerLine(),
                          info.ptr, srcStride, rows, rowBytes);

            // 4) Emit to GUI (no pixmap conversion; viewer draws QImage)
            emit newFrame(frameBGR_);
        }
    }
    catch (const py::error_already_set &e)
    {
        emit error(QString::fromUtf8(e.what()));
    }
    catch (const std::exception &e)
    {
        emit error(QString::fromUtf8(e.what()));
    }
}
