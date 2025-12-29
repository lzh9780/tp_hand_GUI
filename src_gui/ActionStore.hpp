#pragma once
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

/**
 * ActionStore
 * ----------
 * Loads/saves Actions and Action Sets from saveData.json.
 * Schema is the same as your original program:
 * {
 *   "actions":     [ {"name": "...", "joints": [double,...]}, ... ],
 *   "action_sets": [ {"name": "...", "list":   ["Action1","Action2",...]}, ... ]
 * }
 *
 * "joints" is an array of floating values (e.g. angles or currents);
 * in our UI we scale to display/slider units as needed.
 */
class ActionStore
{
public:
    explicit ActionStore(QString jsonPath = QStringLiteral("saveData.json"));

    bool load();
    bool save() const;

    // Actions
    int actionCount() const;
    QString actionName(int idx) const;
    QVector<double> actionJoints(int idx) const;
    void setActionName(int idx, const QString &name);
    void setActionJoints(int idx, const QVector<double> &joints);
    void addAction(const QString &name, const QVector<double> &joints);
    void removeAction(int idx);
    int findActionByName(const QString &name) const;

    // Action sets (structure preserved; UI shows header & collapse)
    int actionSetCount() const;
    QString actionSetName(int idx) const;
    QStringList actionSetList(int idx) const;
    int findActionSetByName(const QString &name) const;

    // Access raw json (if needed)
    const QJsonArray &actions() const { return actions_; }
    const QJsonArray &actionSets() const { return actionSets_; }

private:
    QString path_;
    QJsonArray actions_;
    QJsonArray actionSets_;
};
