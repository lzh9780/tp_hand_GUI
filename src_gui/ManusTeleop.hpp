#pragma once

#include "TeleOperation.hpp"

// #include "../third_party/SDKClient_Linux/SDKClient.hpp"
#include "../third_party/SDKClient_Linux/ClientPlatformSpecific.hpp"
#include "../third_party/SDKClient_Linux/ManusSDK/include/ManusSDK.h"
#include "../third_party/SDKClient_Linux/ManusSDK/include/ManusSDKTypeInitializers.h"

#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>

/// @brief The type of connection to core.
enum class ConnectionType : int
{
    ConnectionType_Invalid = 0,
    ConnectionType_Integrated,
    ConnectionType_Local,
    ConnectionType_Remote,
    ClientState_MAX_CLIENT_STATE_SIZE
};

/// @brief The current state of the client.
enum class ClientState : int
{
    ClientState_Starting = 0,
    ClientState_LookingForHosts,
    ClientState_NoHostsFound,
    ClientState_PickingHost,
    ClientState_ConnectingToCore,
    ClientState_DisplayingData,
    ClientState_Disconnected,

    ClientState_MAX_CLIENT_STATE_SIZE
};

enum class ClientReturnCode : int
{
    ClientReturnCode_Success = 0,
    ClientReturnCode_FailedPlatformSpecificInitialization,
    ClientReturnCode_FailedToResizeWindow,
    ClientReturnCode_FailedToInitialize,
    ClientReturnCode_FailedToFindHosts,
    ClientReturnCode_FailedToConnect,
    ClientReturnCode_UnrecognizedStateEncountered,
    ClientReturnCode_FailedToShutDownSDK,
    ClientReturnCode_FailedPlatformSpecificShutdown,
    ClientReturnCode_FailedToRestart,
    ClientReturnCode_FailedWrongTimeToGetData,

    ClientReturnCode_MAX_CLIENT_RETURN_CODE_SIZE
};

class ManusTeleop : public TeleopBase, public SDKClientPlatformSpecific
{
    Q_OBJECT
public:
    explicit ManusTeleop(QObject *parent = nullptr);
    ~ManusTeleop()
    {
        // Detach from the SDK callbacks FIRST: once s_Instance is null no new
        // callback can enter, and the mutex handshake below guarantees that any
        // callback already inside the critical section has left before we free
        // the landscape buffers.
        {
            std::lock_guard<std::mutex> lock(m_LandscapeMutex);
            m_ShuttingDown = true;
        }
        s_Instance = nullptr;

        // Wait (bounded, so a wedged SDK thread can never hang the GUI) for
        // any callback that grabbed s_Instance before it was cleared.
        for (int t_Try = 0; t_Try < 2000; ++t_Try)
        {
            if (m_CallbacksInFlight.load(std::memory_order_acquire) == 0)
                break;
            std::this_thread::yield();
        }

        stop();

        std::lock_guard<std::mutex> lock(m_LandscapeMutex);
        delete m_Landscape;
        m_Landscape = nullptr;
        delete m_NewLandscape;
        m_NewLandscape = nullptr;
    };

    static void OnErgonomicsCallback(const ErgonomicsStream *const p_Ergo);
    static void OnLandscapeCallback(const Landscape *const p_Landscape);

public slots:
    void start() override;
    void stop() override;
    void onTick() override;

private:
    static ManusTeleop *s_Instance;

    ClientReturnCode InitializeSDK();
    float RoundFloatValue(float p_Value, int p_NumDecimalsToKeep);
    ClientReturnCode UpdateBeforeDisplayingData();

    ManusTimestampInfo m_ErgoTimestampInfo;
    ErgonomicsData m_LeftGloveErgoData;
    ErgonomicsData m_RightGloveErgoData;

    uint32_t m_FirstLeftGloveID = 0;
    uint32_t m_FirstRightGloveID = 0;

    ConnectionType m_ConnectionType = ConnectionType::ConnectionType_Invalid;
    ClientState m_State = ClientState::ClientState_Starting;
    ClientState m_PreviousState = m_State;

    std::mutex m_LandscapeMutex;
    Landscape *m_NewLandscape = nullptr;
    Landscape *m_Landscape = nullptr;
    std::vector<GestureLandscapeData> m_NewGestureLandscapeData;
    std::vector<GestureLandscapeData> m_GestureLandscapeData;

    // Guarded by m_LandscapeMutex. Set before s_Instance is cleared so the
    // SDK callbacks (which may already have read the pointer) bail out.
    bool m_ShuttingDown = false;

    // The SDK invokes the callbacks from its own threads. A callback can pass
    // the s_Instance null check just before the destructor runs, so count the
    // ones in flight and let the destructor wait for them to leave.
    std::atomic<int> m_CallbacksInFlight{0};

    QVector<double> m_LastEmitted;
};
