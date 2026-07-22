#pragma once
#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")
#include <pybind11/embed.h>
#include <pybind11/numpy.h>

#include "../third_party/HandDriver/UDEServer.h"
#include "../third_party/SDKClient_Linux/ManusSDK/include/ManusSDKTypes.h"
// #include "../third_party/SDKClient_Linux/SDKClient.hpp"
#include "../third_party/SDKClient_Linux/ClientPlatformSpecific.hpp"
#include "../third_party/SDKClient_Linux/ManusSDK/include/ManusSDK.h"
#include "../third_party/SDKClient_Linux/ManusSDK/include/ManusSDKTypeInitializers.h"

#include <QTimer>
#include <QObject>
#include <QVector>
#include <QImage>
#include <memory>
#include <mutex>

namespace py = pybind11;

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

class TeleopBase : public QObject
{
    Q_OBJECT
public:
    explicit TeleopBase(QObject *parent = nullptr) : QObject(parent) {};
    virtual ~TeleopBase() { stop(); };

public slots:
    virtual void start() {};
    virtual void stop() {};
    virtual void onTick() {};

signals:
    void newFrame(const QImage &img);
    void newJoint(const QVector<double> &);
    void runningChanged(bool running);
    void status(const QString &msg);
    void error(const QString &msg);

protected:
    QTimer *timer_ = nullptr;
    bool running_ = false;
};

class UDETeleop : public TeleopBase
{
    Q_OBJECT
public:
    explicit UDETeleop(QObject *parent = nullptr);
    ~UDETeleop() { stop(); };

public slots:
    void start() override;
    void stop() override;
    void onTick() override;

private:
    UDEGloveSDK *sdk = nullptr;
};

class PyCamWorker : public TeleopBase
{
    Q_OBJECT
public:
    explicit PyCamWorker(QObject *parent = nullptr);
    ~PyCamWorker() { stop(); };

public slots:
    void start() override;
    void stop() override;
    void onTick() override;

private:
    // Python side
    // std::unique_ptr<py::scoped_interpreter> interp_;
    py::object get_latest_fn;
    py::object get_joint_fn;
    py::object get_wrist_joint_fn;
    py::object stop_fn_;
    py::object start_fn;
    QImage frameBGR_;
};

class ManusTeleop : public TeleopBase, public SDKClientPlatformSpecific
{
    Q_OBJECT
public:
    explicit ManusTeleop(QObject *parent = nullptr);
    ~ManusTeleop() 
    {
        stop(); 
        s_Instance = nullptr;
    };

    static void OnErgonomicsCallback(const ErgonomicsStream* const p_Ergo);
    static void OnLandscapeCallback(const Landscape* const p_Landscape);

public slots:
    void start() override;
    void stop() override;
    void onTick() override;

private:
    static ManusTeleop* s_Instance;

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
	Landscape* m_NewLandscape = nullptr;
	Landscape* m_Landscape = nullptr;
    std::vector<GestureLandscapeData> m_NewGestureLandscapeData;
	std::vector<GestureLandscapeData> m_GestureLandscapeData;
};
