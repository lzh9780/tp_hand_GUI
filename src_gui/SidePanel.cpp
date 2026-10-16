#include "SidePanel.h"
#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QSlider>
#include <QLabel>
#include <QWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QComboBox>
#include <QTabWidget>
#include <QVector>
#include <cmath>

namespace
{
QString formatValue(float v)
{
    return QString::number(double(v), 'f', 2);
}
}

SidePanel::SidePanel(QWidget *parent)
    : QFrame(parent)
{
    setProperty("floatingPanel", QVariant(true));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildSliderPanel();
    root->addWidget(sliderPanel_);
}

void SidePanel::buildSliderPanel()
{
    sliderPanel_ = new QWidget;
    auto *sliderRoot = new QVBoxLayout(sliderPanel_);
    sliderRoot->setContentsMargins(6, 6, 6, 6);
    sliderRoot->setSpacing(4);

    // "Execute" pushes the whole slider set at once; "Reset" parks every slider
    // at zero. The former Follow/Setup mode switch was removed: targets are now
    // always sent immediately, and the connection bar arms the hand.
    auto *resetBtn = new QPushButton();
    resetBtn->setText("Reset");

    auto *btnWidget = new QWidget;
    QHBoxLayout *h = new QHBoxLayout(btnWidget);
    h->addWidget(resetBtn);
    h->addStretch(1);
    h->setContentsMargins(0, 0, 0, 0);

    sliderRoot->addWidget(btnWidget);

    sliderScroll_ = new QScrollArea;
    sliderScroll_->setWidgetResizable(true);
    sliderScroll_->setFrameShape(QFrame::NoFrame);
    rebuildSliderHost();
    sliderRoot->addWidget(sliderScroll_);

    connect(resetBtn, &QPushButton::clicked, this, &SidePanel::onReset);
}

void SidePanel::rebuildSliderHost()
{
    // Every joint row lives under a single content widget, so rebuilding the
    // host is enough to drop the whole tree: Qt unwinds parent/child
    // ownership for us instead of a hand-rolled recursive delete.
    auto *sliderWidget = new QWidget;
    sliderVBox_ = new QVBoxLayout(sliderWidget);
    sliderVBox_->setAlignment(Qt::AlignTop);
    sliderScroll_->setWidget(sliderWidget);
}

void SidePanel::loadSliderModule(QString name, float min, float max)
{
    auto *hl = new QHBoxLayout();
    hl->setContentsMargins(6, 2, 6, 2);
    auto *nameLbl = new QLabel(name);
    nameLabels_.append(nameLbl);
    auto *led = new LedIndicator();
    led->setFixedSize(20, 20);
    leds_.append(led);
    auto *valEnter = new QLineEdit("0.00");
    valEnter->setFixedWidth(75);
    valEnter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabels_.append(valEnter);
    auto *currValueLbl = new QLabel("  (0.00)");
    currValueLabels_.append(currValueLbl);
    hl->addWidget(nameLbl);
    hl->addStretch(1);
    hl->addWidget(led);
    hl->addWidget(valEnter);
    hl->addWidget(currValueLbl);
    sliderVBox_->addLayout(hl);

    // Slider row: [min .. max]
    auto *sl = new QHBoxLayout();
    sl->setContentsMargins(12, 0, 12, 8);
    auto *sMin = new QLabel(QString::number(min));
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange((int)(min * 100.0), (int)(max * 100.0));
    slider->setValue(0);
    slider->setEnabled(false);
    auto *sMax = new QLabel(QString::number(max));
    sl->addWidget(sMin);
    sl->addWidget(slider);
    sl->addWidget(sMax);
    sliders_.append(slider);
    sliderVBox_->addLayout(sl);
    valueLbls.append(std::pair(sMin, sMax));
    control_modes.append("zero_torque");

    connect(slider, &QSlider::valueChanged, this, &SidePanel::onAnySliderChanged);
    connect(valEnter, &QLineEdit::editingFinished, this, &SidePanel::onValueEditChanged);
}

void SidePanel::loadHandControllor(grip::HandSettings *hand_cfg)
{
    // The former "Advance" button (per-joint control-mode panel) is gone: the
    // connection bar's Enable/Disable now drives the whole hand.
    for (auto s : hand_cfg->structs)
    {
        auto *mode_lable = new QLabel(s.first.c_str());
        sliderVBox_->addWidget(mode_lable);

        for (auto j : s.second)
        {
            if (j.id <= 0) continue;
            loadSliderModule(QString(j.name.c_str()), j.route.first, j.route.second);
            // loadSliderModule appends exactly one slider, so the new slider
            // sits at the last index. Value readouts are keyed by joint name.
            sliderIndex_.insert(QString::fromStdString(j.name), sliders_.size() - 1);
        }
    }
}

// -------- helpers --------
QList<int> SidePanel::jointDegrees() const
{
    QList<int> out;
    out.reserve(sliders_.size());
    for (auto *s : sliders_)
        out << s->value();
    return out;
}

void SidePanel::onAnySliderChanged()
{
    for (int i = 0; i < sliders_.size() && i < valueLabels_.size() && i < control_modes.size(); ++i)
    {
        if (control_modes[i] == "joint_position")
            valueLabels_[i]->setText(QString("%1").arg((float)sliders_[i]->value() / 100.0, 0, 'f', 2));
        else if (control_modes[i] == "hall_position")
            valueLabels_[i]->setText(QString("%1").arg((int)sliders_[i]->value()));
        else
            valueLabels_[i]->setText(QString("0"));
    }
    emit valuesChanged();
}

void SidePanel::onValueEditChanged()
{
    // Only push back the row the user actually edited. Re-applying every row
    // re-emitted valueChanged for each slider and flooded the bus with
    // redundant targets.
    auto *edit = qobject_cast<QLineEdit *>(sender());
    const int edited = edit ? valueLabels_.indexOf(edit) : -1;

    const int count = std::min({valueLabels_.size(), sliders_.size(), control_modes.size()});
    for (int i = 0; i < count; ++i)
    {
        if (edited >= 0 && i != edited)
            continue;

        if (control_modes[i] == "joint_position")
            sliders_[i]->setValue((int)(valueLabels_[i]->text().toDouble() * 100.0));
        else if (control_modes[i] == "hall_position")
            sliders_[i]->setValue(valueLabels_[i]->text().toInt());
        else
            sliders_[i]->setValue(0);
    }
}

void SidePanel::onReset()
{
    for (auto *s : sliders_)
        s->setValue(0);
}

void SidePanel::applyActionValues(const QVector<double> &values)
{
    const int count = std::min({sliders_.size(), values.size(), valueLbls.size()});
    for (int i = 0; i < count; ++i)
    {
        const float min = valueLbls[i].first->text().toFloat() * 100;
        const float max = valueLbls[i].second->text().toFloat() * 100;
        const float v = std::clamp(float(values[i]), min, max);
        sliders_[i]->setValue((int)v);
    }
    emit valuesChanged();
}

void SidePanel::applyCameraData(std::unordered_map<std::string, float> &data, grip::HandSettings *hand_cfg)
{
    // Hot path: this runs once per tele-operation sample (up to ~120 Hz).
    // Use the prebuilt name -> row index map instead of a linear scan that
    // rebuilt a QByteArray (toUtf8) for every single comparison.
    const int count = std::min({nameLabels_.size(), sliders_.size(), valueLbls.size(), control_modes.size()});

    for (const auto &[name, value] : data)
    {
        const int i = sliderIndex_.value(QString::fromStdString(name), -1);
        if (i < 0 || i >= count)
            continue;

        float min = valueLbls[i].first->text().toFloat();
        float max = valueLbls[i].second->text().toFloat();
        float v = std::clamp(float(value), float(min), float(max));

        if (control_modes[i] == "hall_position")
        {
            // Cache the joint -> scale lookup; scanning every struct on every
            // sample made this quadratic.
            auto scaleIt = hallScales_.find(name);
            if (scaleIt == hallScales_.end())
            {
                float scale = 0.0f;
                for (auto [key, vec] : hand_cfg->structs)
                {
                    for (auto joint : vec)
                    {
                        if (joint.name == name)
                            scale = (joint.hall_route.second - joint.hall_route.first) /
                                    (joint.route.second - joint.route.first) / 100.0f;
                    }
                }
                scaleIt = hallScales_.emplace(name, scale).first;
            }
            v = v * scaleIt->second;
        }
        else if (control_modes[i] == "zero_torque")
            continue;

        sliders_[i]->setValue((int)(v * 100));
    }
}

void SidePanel::setAllJointsEnabled(bool enabled)
{
    // One mode for the whole hand: the connection bar's Enable/Disable button
    // replaced the per-module control-mode combo boxes.
    for (int i = 0; i < sliders_.size() && i < control_modes.size(); ++i)
    {
        control_modes[i] = enabled ? "joint_position" : "zero_torque";
        sliders_[i]->setEnabled(enabled);
        if (i < valueLabels_.size())
        {
            valueLabels_[i]->setReadOnly(!enabled);
            if (!enabled)
                valueLabels_[i]->setText(QString("0"));
        }
    }
}

void SidePanel::removeAll()
{
    sliders_.clear();
    valueLabels_.clear();
    currValueLabels_.clear();
    nameLabels_.clear();
    valueLbls.clear();
    leds_.clear();
    hallScales_.clear();
    control_modes.clear();
    sliderIndex_.clear();

    if (sliderScroll_)
    {
        if (QWidget *previous = sliderScroll_->takeWidget())
            delete previous;
        rebuildSliderHost();
    }
}

QStringList SidePanel::getControlModes()
{
    return control_modes;
}
