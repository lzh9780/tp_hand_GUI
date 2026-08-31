#include "UDETeleop.hpp"

UDETeleop::UDETeleop(QObject *parent) : TeleopBase(parent)
{
    sdk = new UDEGloveSDK();
    sdk->Initialize();
}

void UDETeleop::start()
{
    if (running_)
        return;

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &UDETeleop::onTick);
    timer_->start(1000 / 120);

    // A dropped acknowledgement must never stall the pipeline.
    resetDelivery();

    sdk->StartListening();
    cout << "Server Start Listening.." << endl;

    running_ = true;
    emit status("Glove receive loop started.");
    emit runningChanged(running_);
}

void UDETeleop::stop()
{
    // Clean up unconditionally: the destructor calls stop() even when the
    // worker was never started, and the early return used to leak the SDK.
    if (timer_)
    {
        timer_->stop();
        // deleteLater() is posted to the worker thread, which is stopped
        // before this object is destroyed - it would never be processed.
        delete timer_;
        timer_ = nullptr;
    }

    if (sdk)
    {
        try
        {
            if (sdk->GetStatus() != ServerStatus::END)
                sdk->EndListening();
        }
        catch (const std::exception &e)
        {
            // report but continue shutdown
            emit error(QString::fromUtf8(e.what()));
        }
        delete sdk;
        sdk = nullptr;
    }

    if (!running_)
        return;

    running_ = false;
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

            // Drop the sample if the GUI has not consumed the previous one yet.
            if (requestJointDelivery())
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
