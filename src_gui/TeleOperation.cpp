#include "TeleOperation.hpp"

UDETeleop::UDETeleop(QObject *parent) : TeleopBase(parent)
{
    sdk = new UDEGloveSDK();
    sdk->Initialize();
}

void UDETeleop::start()
{
    if (running_)
        return;

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

ManusTeleop* ManusTeleop::s_Instance = nullptr;

ManusTeleop::ManusTeleop(QObject *parent) : TeleopBase(parent)
{
    s_Instance = this;
    
    // using initializers like these ensure that the data is set to its default values.
    ErgonomicsData_Init(&m_LeftGloveErgoData);
    ErgonomicsData_Init(&m_RightGloveErgoData);

    // TestTimestamp();
}

void ManusTeleop::start()
{
    if (running_)
        return;

    // if (!PlatformSpecificInitialization())
    // {
    //     return; // ClientReturnCode::ClientReturnCode_FailedPlatformSpecificInitialization;
    // }

    const ClientReturnCode t_IntializeResult = InitializeSDK();
    if (t_IntializeResult != ClientReturnCode::ClientReturnCode_Success)
    {
        // ClientLog::error("Failed to initialize the SDK. Are you sure the correct ManusSDKLibary is used?");
        emit error("Failed to initialize the SDK. Are you sure the correct ManusSDKLibary is used?");
        return; // ClientReturnCode::ClientReturnCode_FailedToInitialize;
    }

    SDKReturnCode t_ConnectResult = SDKReturnCode::SDKReturnCode_Error;
    ManusHost t_Empty;
    ManusHost_Init(&t_Empty);
    t_ConnectResult = CoreSdk_ConnectToHost(t_Empty);

    if (t_ConnectResult == SDKReturnCode::SDKReturnCode_NotConnected)
	{
		m_State = ClientState::ClientState_NoHostsFound;
	}
	if (t_ConnectResult != SDKReturnCode::SDKReturnCode_Success)
	{
		emit error(QString("Failed to connect to Core. The error given was {}.").arg((int32_t)t_ConnectResult));
		return;// ClientReturnCode::ClientReturnCode_FailedToConnect;
	}

    // while (true)
    // {
    //     UpdateBeforeDisplayingData();
    // }

    timer_ = new QTimer();
    connect(timer_, &QTimer::timeout, this, &ManusTeleop::onTick);
    timer_->start(1000 / 120);

    running_ = true;
    emit runningChanged(running_);
}

ClientReturnCode ManusTeleop::InitializeSDK()
{
    m_ConnectionType = ConnectionType::ConnectionType_Integrated;
    m_State = ClientState::ClientState_ConnectingToCore;

    // Invalid connection type detected
    if (m_ConnectionType == ConnectionType::ConnectionType_Invalid || m_ConnectionType == ConnectionType::ClientState_MAX_CLIENT_STATE_SIZE)
        return ClientReturnCode::ClientReturnCode_FailedToInitialize;

    // before we can use the SDK, some internal SDK bits need to be initialized.
    // however after initializing, the SDK is not yet connected to a host or doing anything network related just yet.
    SDKReturnCode t_InitializeResult;
    t_InitializeResult = CoreSdk_InitializeIntegrated();

    if (t_InitializeResult != SDKReturnCode::SDKReturnCode_Success)
    {
        // ClientLog::error("Failed to initialize the Manus Core SDK. The value returned was {}.", (int32_t)t_InitializeResult);
        emit error(QString("Failed to initialize the Manus Core SDK. The value returned was %1.").arg((int32_t)t_InitializeResult));
        return ClientReturnCode::ClientReturnCode_FailedToInitialize;
    }

    // const ClientReturnCode t_CallBackResults = RegisterAllCallbacks();
    // if (t_CallBackResults != ::ClientReturnCode::ClientReturnCode_Success)
    // {
    //     // ClientLog::error("Failed to initialize callbacks.");
    //     return t_CallBackResults;
    // }

    const SDKReturnCode t_RegisterLandscapeCallbackResult = CoreSdk_RegisterCallbackForLandscapeStream(*OnLandscapeCallback);
	if (t_RegisterLandscapeCallbackResult != SDKReturnCode::SDKReturnCode_Success)
	{
		emit error(QString("Failed to register callback for landscape from Manus Core. The value returned was %1.").arg((int32_t)t_RegisterLandscapeCallbackResult));
		return ClientReturnCode::ClientReturnCode_FailedToInitialize;
	}

    const SDKReturnCode t_RegisterErgonomicsCallbackResult = CoreSdk_RegisterCallbackForErgonomicsStream(*OnErgonomicsCallback);
    if (t_RegisterErgonomicsCallbackResult != SDKReturnCode::SDKReturnCode_Success)
    {
        // ClientLog::error("Failed to register callback function for ergonomics data from Manus Core. The value returned was {}.", (int32_t)t_RegisterErgonomicsCallbackResult);
        emit error(QString("Failed to register callback function for ergonomics data from Manus Core. The value returned was %1.").arg((int32_t)t_RegisterErgonomicsCallbackResult));
        return ClientReturnCode::ClientReturnCode_FailedToInitialize;
    }

    // after everything is registered and initialized as seen above
    // we must also set the coordinate system being used for the data in this client.
    // (each client can have their own settings. unreal and unity for instance use different coordinate systems)
    // if this is not set, the SDK will not connect to any Manus core host.
    // The coordinate system used for this example is z-up, x-positive, right-handed and in meter scale.
    CoordinateSystemVUH t_VUH;
    CoordinateSystemVUH_Init(&t_VUH);
    t_VUH.handedness = Side::Side_Right;
    t_VUH.up = AxisPolarity::AxisPolarity_PositiveZ;
    t_VUH.view = AxisView::AxisView_XFromViewer;
    t_VUH.unitScale = 1.0f; // 1.0 is meters, 0.01 is cm, 0.001 is mm.

    // The above specified coordinate system is used to initialize and the coordinate space is specified (world/local).
    const SDKReturnCode t_CoordinateResult = CoreSdk_InitializeCoordinateSystemWithVUH(t_VUH, true);

    /* this is an example if you want to use the other coordinate system instead of VUH (view, up, handedness)
    CoordinateSystemDirection t_Direction;
    t_Direction.x = AxisDirection::AD_Right;
    t_Direction.y = AxisDirection::AD_Up;
    t_Direction.z = AxisDirection::AD_Forward;
    const SDKReturnCode t_InitializeResult = CoreSdk_InitializeCoordinateSystemWithDirection(t_Direction, true);
    */

    if (t_CoordinateResult != SDKReturnCode::SDKReturnCode_Success)
    {
        // ClientLog::error("Failed to initialize the Manus Core SDK coordinate system. The value returned was {}.", (int32_t)t_InitializeResult);
        emit error(QString("Failed to initialize the Manus Core SDK coordinate system. The value returned was %1.").arg((int32_t)t_InitializeResult));
        return ClientReturnCode::ClientReturnCode_FailedToInitialize;
    }

    return ClientReturnCode::ClientReturnCode_Success;
}

void ManusTeleop::OnErgonomicsCallback(const ErgonomicsStream *const p_Ergo)
{
    if (s_Instance)
    {
        for (uint32_t i = 0; i < p_Ergo->dataCount; i++)
        {
            if (p_Ergo->data[i].isUserID)
                continue;

            ErgonomicsData *t_Ergo = nullptr;
            if (p_Ergo->data[i].id == s_Instance->m_FirstLeftGloveID)
            {
                t_Ergo = &s_Instance->m_LeftGloveErgoData;
            }
            if (p_Ergo->data[i].id == s_Instance->m_FirstRightGloveID)
            {
                t_Ergo = &s_Instance->m_RightGloveErgoData;
            }
            if (t_Ergo == nullptr)
                continue;
            CoreSdk_GetTimestampInfo(p_Ergo->publishTime, &s_Instance->m_ErgoTimestampInfo);
            t_Ergo->id = p_Ergo->data[i].id;
            t_Ergo->isUserID = p_Ergo->data[i].isUserID;
            for (int j = 0; j < ErgonomicsDataType::ErgonomicsDataType_MAX_SIZE; j++)
            {
                t_Ergo->data[j] = p_Ergo->data[i].data[j];
            }
        }
    }
}

ClientReturnCode ManusTeleop::UpdateBeforeDisplayingData()
{
    m_LandscapeMutex.lock();
	if (m_NewLandscape != nullptr)
	{
		if (m_Landscape != nullptr)
		{
			delete m_Landscape;
		}
		m_Landscape = m_NewLandscape;
		m_NewLandscape = nullptr;
		m_GestureLandscapeData.swap(m_NewGestureLandscapeData);
	}
	m_LandscapeMutex.unlock();

    m_FirstLeftGloveID = 0;
    m_FirstRightGloveID = 0;
    if (m_Landscape == nullptr)
        return ClientReturnCode::ClientReturnCode_Success;
    for (size_t i = 0; i < m_Landscape->gloveDevices.gloveCount; i++)
    {
        if (m_FirstLeftGloveID == 0 && m_Landscape->gloveDevices.gloves[i].side == Side::Side_Left)
        {
            m_FirstLeftGloveID = m_Landscape->gloveDevices.gloves[i].id;
            continue;
        }
        if (m_FirstRightGloveID == 0 && m_Landscape->gloveDevices.gloves[i].side == Side::Side_Right)
        {
            m_FirstRightGloveID = m_Landscape->gloveDevices.gloves[i].id;
            continue;
        }
    }

    return ClientReturnCode::ClientReturnCode_Success;
}

float ManusTeleop::RoundFloatValue(float p_Value, int p_NumDecimalsToKeep)
{
    // Since C++11, powf is supposed to be declared in <cmath>.
    // Unfortunately, gcc decided to be non-compliant on this for no apparent
    // reason, so now we have to do this.
    // https://stackoverflow.com/questions/5483930/powf-is-not-a-member-of-std
    float t_Power = static_cast<float>(std::pow(
        10.0,
        static_cast<double>(p_NumDecimalsToKeep)));
    return std::round(p_Value * t_Power) / t_Power;
}

void ManusTeleop::stop()
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
        // 关闭连接，停止内部线程，释放资源
        SDKReturnCode t_Result = CoreSdk_ShutDown();
        if (t_Result != SDKReturnCode::SDKReturnCode_Success)
        {
            emit error(QString("CoreSdk_ShutDown failed with code: %1")
                       .arg(static_cast<int>(t_Result)));
        }

        // 3. 平台特定清理（如 ncurses 终端恢复）
        // if (!PlatformSpecificShutdown())
        // {
        //     emit error("PlatformSpecificShutdown failed.");
        // }
    }
    catch (const std::exception &e)
    {
        emit error(QString::fromUtf8(e.what()));
    }

    emit status("Python camera loop stopped.");
    emit runningChanged(running_);
}

void ManusTeleop::onTick()
{
    if (!running_)
        return;

    UpdateInput();

    Json::Value::Members list;
    try
    {
        UpdateBeforeDisplayingData();

        QVector<double> l;
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[1], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[0], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[3], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[2], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[5], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[6], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[7], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[4], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[9], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[10], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[11], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[8], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[13], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[14], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[15], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[12], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[17], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[18], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[19], 2));
        l.append(RoundFloatValue(m_LeftGloveErgoData.data[16], 2));
        l.append(0);
        l.append(0);
        l.append(0);
        l.append(0);

        emit newJoint(l);

        list.clear();
    }
    catch (const std::exception &e)
    {
        // std::cerr << e.what() << '\n';
        emit error(QString::fromUtf8(e.what()));
    }
}

/// @brief This gets called when receiving landscape information from core
/// @param p_Landscape contains the new landscape from core.
void ManusTeleop::OnLandscapeCallback(const Landscape* const p_Landscape)
{
	if (s_Instance == nullptr)return;

	Landscape* t_Landscape = new Landscape(*p_Landscape);
	s_Instance->m_LandscapeMutex.lock();
	if (s_Instance->m_NewLandscape != nullptr) delete s_Instance->m_NewLandscape;
	s_Instance->m_NewLandscape = t_Landscape;
	s_Instance->m_NewGestureLandscapeData.resize(t_Landscape->gestureCount);
	CoreSdk_GetGestureLandscapeData(s_Instance->m_NewGestureLandscapeData.data(), (uint32_t)s_Instance->m_NewGestureLandscapeData.size());
	s_Instance->m_LandscapeMutex.unlock();
}


