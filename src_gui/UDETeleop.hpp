#pragma once

#include "TeleOperation.hpp"

#include "../third_party/HandDriver/UDEServer.h"
#include "../third_party/SDKClient_Linux/ManusSDK/include/ManusSDKTypes.h"

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

