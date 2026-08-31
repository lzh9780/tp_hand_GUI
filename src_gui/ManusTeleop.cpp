#include "ManusTeleop.hpp"

#include <algorithm>

ManusTeleop *ManusTeleop::s_Instance = nullptr;

namespace
{
// RAII bookkeeping for the SDK callback threads. Keeps the in-flight count
// raised for the whole callback, including early returns, so that
// ~ManusTeleop can safely wait for callbacks that already captured the
// instance pointer before it was cleared.
class CallbackGuard
{
public:
    explicit CallbackGuard(std::atomic<int> &p_Counter) : m_Counter(p_Counter)
    {
        m_Counter.fetch_add(1, std::memory_order_acq_rel);
    }
    ~CallbackGuard() { m_Counter.fetch_sub(1, std::memory_order_release); }

    CallbackGuard(const CallbackGuard &) = delete;
    CallbackGuard &operator=(const CallbackGuard &) = delete;

private:
    std::atomic<int> &m_Counter;
};
} // namespace

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
        // stop() short-circuits while running_ is false, so release the SDK
        // here - otherwise every retry would initialise it again.
        CoreSdk_ShutDown();
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
        emit error(QString("Failed to connect to Core. The error given was %1.").arg((int32_t)t_ConnectResult));
        CoreSdk_ShutDown();
        return; // ClientReturnCode::ClientReturnCode_FailedToConnect;
    }

    // while (true)
    // {
    //     UpdateBeforeDisplayingData();
    // }

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &ManusTeleop::onTick);
    timer_->start(1000 / 120);

    // A dropped acknowledgement must never stall the pipeline.
    resetDelivery();

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
    // Runs on the SDK's own thread.
    ManusTeleop *t_Instance = s_Instance;
    if (t_Instance != nullptr)
    {
        // Keeps this callback visible to ~ManusTeleop until we return.
        CallbackGuard t_Guard(t_Instance->m_CallbacksInFlight);

        // The destructor may have cleared s_Instance between the two reads.
        if (t_Instance->m_ShuttingDown)
            return;

        for (uint32_t i = 0; i < p_Ergo->dataCount; i++)
        {
            if (p_Ergo->data[i].isUserID)
                continue;

            ErgonomicsData *t_Ergo = nullptr;
            if (p_Ergo->data[i].id == t_Instance->m_FirstLeftGloveID)
            {
                t_Ergo = &t_Instance->m_LeftGloveErgoData;
            }
            if (p_Ergo->data[i].id == t_Instance->m_FirstRightGloveID)
            {
                t_Ergo = &t_Instance->m_RightGloveErgoData;
            }
            if (t_Ergo == nullptr)
                continue;
            CoreSdk_GetTimestampInfo(p_Ergo->publishTime, &t_Instance->m_ErgoTimestampInfo);
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
    {
        // RAII: the manual lock/unlock above leaked the mutex if a swap threw.
        std::lock_guard<std::mutex> t_Lock(m_LandscapeMutex);
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
    }

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
    if (timer_)
    {
        timer_->stop();
        // deleteLater() is posted to this (worker) thread, which is stopped
        // before the object is destroyed - it would never be processed.
        delete timer_;
        timer_ = nullptr;
    }

    if (!running_)
        return;
    running_ = false;
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

    try
    {
        UpdateBeforeDisplayingData();

        // onTick() indexes fixed slots up to 19. Bound every read by the
        // SDK's own element count so a version mismatch returns 0.0f instead
        // of reading past the end of the array.
        constexpr int kErgoSlots =
            static_cast<int>(ErgonomicsDataType::ErgonomicsDataType_MAX_SIZE);
        auto ergoAt = [this](int p_Index) -> float
        {
            return (p_Index >= 0 && p_Index < kErgoSlots)
                       ? m_LeftGloveErgoData.data[p_Index]
                       : 0.0f;
        };

        QVector<double> l;
        l.append(RoundFloatValue(ergoAt(1), 2));
        l.append(RoundFloatValue(ergoAt(0), 2));
        l.append(RoundFloatValue(ergoAt(3), 2));
        l.append(RoundFloatValue(ergoAt(2), 2));
        l.append(RoundFloatValue(ergoAt(5), 2));
        l.append(RoundFloatValue(ergoAt(6), 2));
        l.append(RoundFloatValue(ergoAt(7), 2));
        l.append(RoundFloatValue(ergoAt(4), 2));
        l.append(RoundFloatValue(ergoAt(9), 2));
        l.append(RoundFloatValue(ergoAt(10), 2));
        l.append(RoundFloatValue(ergoAt(11), 2));
        l.append(RoundFloatValue(ergoAt(8), 2));
        l.append(RoundFloatValue(ergoAt(13), 2));
        l.append(RoundFloatValue(ergoAt(14), 2));
        l.append(RoundFloatValue(ergoAt(15), 2));
        l.append(RoundFloatValue(ergoAt(12), 2));
        l.append(RoundFloatValue(ergoAt(17), 2));
        l.append(RoundFloatValue(ergoAt(18), 2));
        l.append(RoundFloatValue(ergoAt(19), 2));
        l.append(RoundFloatValue(ergoAt(16), 2));
        l.append(0);
        l.append(0);
        l.append(0);
        l.append(0);

        // Drop the sample if the GUI has not consumed the previous one yet.
        // Without this the worker keeps posting faster than the GUI can drain
        // and the event queue grows until the process dies.
        if (requestJointDelivery())
        {
            if (l == m_LastEmitted)
            {
                // Idle glove: nothing new to show. Release the slot, nobody
                // will acknowledge a sample that was never delivered.
                acknowledgeJoints();
            }
            else
            {
                m_LastEmitted = l;
                emit newJoint(l);
            }
        }
    }
    catch (const std::exception &e)
    {
        // std::cerr << e.what() << '\n';
        emit error(QString::fromUtf8(e.what()));
    }
}

/// @brief This gets called when receiving landscape information from core
/// @param p_Landscape contains the new landscape from core.
void ManusTeleop::OnLandscapeCallback(const Landscape *const p_Landscape)
{
    // Runs on the SDK's own thread.
    ManusTeleop *t_Instance = s_Instance;
    if (t_Instance == nullptr)
        return;

    // Keeps this callback visible to ~ManusTeleop until we return.
    CallbackGuard t_Guard(t_Instance->m_CallbacksInFlight);

    // The destructor may have cleared s_Instance between the two reads above.
    if (t_Instance->m_ShuttingDown)
        return;

    Landscape *t_Landscape = new Landscape(*p_Landscape);

    // Query the SDK before taking our lock: CoreSdk_* calls may acquire
    // internal SDK locks, and calling them while holding m_LandscapeMutex
    // invites a deadlock with the SDK's own threads.
    std::vector<GestureLandscapeData> t_Gestures;
    t_Gestures.resize(t_Landscape->gestureCount);
    if (!t_Gestures.empty())
        CoreSdk_GetGestureLandscapeData(t_Gestures.data(), (uint32_t)t_Gestures.size());

    std::lock_guard<std::mutex> t_Lock(t_Instance->m_LandscapeMutex);

    // The object may be tearing down on another thread; re-check under the
    // mutex so we never touch freed buffers.
    if (t_Instance->m_ShuttingDown)
    {
        delete t_Landscape;
        return;
    }

    if (t_Instance->m_NewLandscape != nullptr)
        delete t_Instance->m_NewLandscape;
    t_Instance->m_NewLandscape = t_Landscape;
    t_Instance->m_NewGestureLandscapeData.swap(t_Gestures);
}