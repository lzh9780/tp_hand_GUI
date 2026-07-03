#include "TeleOperation.hpp"

TeleOperation::TeleOperation(QObject *parent) : QObject(parent)
{
    sdk = new UDEGloveSDK();
    sdk->Initialize();
}

void TeleOperation::start()
{
    timer_ = new QTimer();
    connect(timer_, &QTimer::timeout, this, &TeleOperation::onTick);
    timer_->start(1000 / 120);

    sdk->StartListening();
    cout << "Server Start Listening.." << endl;

    running_ = true;
    emit status("Glove receive loop started.");
    emit runningChanged(running_);
}

void TeleOperation::stop()
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
    catch (const std::exception& e)
    {
        // report but continue shutdown
        emit error(QString::fromUtf8(e.what()));
    }
    emit status("Glove receive loop stopped.");
    emit runningChanged(running_);
}


void TeleOperation::onTick()
{
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
    catch (const std::exception& e)
    {
        // std::cerr << e.what() << '\n';
        emit error(QString::fromUtf8(e.what()));
    }
}
