#pragma once
#include <QDialog>
#include <QPair>

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QGroupBox;
class QPushButton;
class QComboBox;
class QTabWidget;
class QTextBrowser;
class QVBoxLayout;

/**
 * SettingsWindow (GUI)
 * --------------------
 * Pop-up with one tab per topic:
 *   - CAN:      CAN bus parameters + simulation mode (both moved out of Connector)
 *   - 3D Model: single model (driven by feedback *or* command data) or dual model
 *   - Filter:   feedback filter / One Euro parameters (moved out of SidePanel)
 *   - Software: read-only build / environment information
 *
 * Nothing is pushed to the device until "Apply" is pressed, and every applied
 * change is reported through notify() so it shows up in the MainWindow log.
 *
 * Signals:
 *  - canConfigChanged(...): CAN bus parameters
 *  - simulationModeChanged(on): "no hardware connected" switch
 *  - filterChanged(...):    filter enable flag + parameters
 *  - filterReset():         clear the stored filter history
 *  - notify(text):          status lines for the MainWindow log
 */
class SettingsWindow : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    // Seed the CAN fields without emitting (used when a config is loaded).
    void setCanConfig(int device_type, int device_index, int channel_index,
                      int core_clock_hz, bool termination_enabled,
                      int arb_tseg1, int arb_tseg2, int arb_sjw, int arb_brp,
                      int data_tseg1, int data_tseg2, int data_sjw, int data_brp);

    // Push the current values out; MainWindow uses this to seed can_cfg_.
    void applyCanConfig();
    void applyFilter();

    // Lock the CAN section while a device is connected.
    void setConnected(bool connected);

    bool filterEnabled() const;
    double filterFreq() const;
    double filterMinCutoff() const;
    double filterBeta() const;
    double filterDCutoff() const;

    bool simulationMode() const;

    // --- 3D model ---
    bool dualModel() const;       // true = two instances (cmd + fb), false = single
    bool useFeedbackData() const; // single model only: feedback or command driven

    /* Navigation style of the 3D view, named after RViz' view controller. */
    QString viewStyle() const;
    /* Draw the camera's look-at point (orbit pivot) in the 3D view. */
    bool showFocalMarker() const;

    /* Fill the "Software" tab (config file, available models, …). */
    void setSoftwareInfo(const QString &configPath, const QStringList &models);

    /* Device index / channel live on the top connection bar; keep a copy so the
       CAN signal can echo them back when the other fields are applied. */
    void setDeviceLocation(int device_index, int channel_index);

signals:
    void canConfigChanged(int device_type, int device_index, int channel_index,
                          int core_clock_hz, bool termination_enabled,
                          int arb_tseg1, int arb_tseg2, int arb_sjw, int arb_brp,
                          int data_tseg1, int data_tseg2, int data_sjw, int data_brp);
    void filterChanged(bool enabled, double freq, double min_cutoff, double beta, double d_cutoff);
    void filterReset();
    void simulationModeChanged(bool simulation);
    void model3DConfigChanged(bool dual, bool use_feedback);
    void viewStyleChanged(const QString &style);
    void focalMarkerVisibilityChanged(bool visible);
    void notify(const QString &msg);

private slots:
    void onApply();
    void onResetFilterState();
    void onModelModeChanged(int index);

private:
    void buildCanSection(QVBoxLayout *root);
    void buildFilterSection(QVBoxLayout *root);
    void buildModelSection(QVBoxLayout *root);
    void buildSoftwareSection(QVBoxLayout *root);
    void emitCanConfig();

    QTabWidget *tabs_{nullptr};

    /* CAN bus */
    QGroupBox *canGroup_{nullptr};
    QSpinBox *deviceType_{nullptr};
    QSpinBox *coreClock_{nullptr};
    QSpinBox *arbTseg1_{nullptr};
    QSpinBox *arbTseg2_{nullptr};
    QSpinBox *arbSjw_{nullptr};
    QSpinBox *arbBrp_{nullptr};
    QSpinBox *dataTseg1_{nullptr};
    QSpinBox *dataTseg2_{nullptr};
    QSpinBox *dataSjw_{nullptr};
    QSpinBox *dataBrp_{nullptr};
    QCheckBox *terminationBox{nullptr};

    /* Simulation mode (was part of Connector) */
    QCheckBox *simulationBox_{nullptr};

    /* Feedback filter */
    QGroupBox *filterGroup_{nullptr};
    QCheckBox *filterEnable_{nullptr};
    QDoubleSpinBox *filterFreq_{nullptr};
    QDoubleSpinBox *filterMinCutoff_{nullptr};
    QDoubleSpinBox *filterBeta_{nullptr};
    QDoubleSpinBox *filterDCutoff_{nullptr};

    /* Mirrored from the connection bar (device index, channel). */
    QPair<int, int> deviceLocation_{0, 0};

    /* 3D model */
    QComboBox *model3DMode_{nullptr};
    QComboBox *model3DSource_{nullptr};
    QComboBox *viewStyle_{nullptr};
    QCheckBox *showFocalMarker_{nullptr};

    /* Software information (read-only) */
    QTextBrowser *infoView_{nullptr};
};
