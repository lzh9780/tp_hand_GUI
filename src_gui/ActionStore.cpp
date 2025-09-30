#include "ActionStore.hpp"
#include <QFile>
#include <QJsonDocument>

ActionStore::ActionStore(QString jsonPath) : path_(std::move(jsonPath)) {}

bool ActionStore::load()
{
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly))
    {
        actions_ = QJsonArray();
        actionSets_ = QJsonArray();
        return false;
    }
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return false;
    auto obj = doc.object();
    actions_ = obj.value("actions").toArray();
    actionSets_ = obj.value("action_sets").toArray();
    return true;
}

bool ActionStore::save() const
{
    QJsonObject root;
    root.insert("actions", actions_);
    root.insert("action_sets", actionSets_);
    QFile f(path_);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QJsonDocument d(root);
    f.write(d.toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

// -------- Actions --------
int ActionStore::actionCount() const { return actions_.size(); }

QString ActionStore::actionName(int idx) const
{
    if (idx < 0 || idx >= actions_.size())
        return {};
    return actions_[idx].toObject().value("name").toString();
}

QVector<double> ActionStore::actionJoints(int idx) const
{
    QVector<double> out;
    if (idx < 0 || idx >= actions_.size())
        return out;
    auto a = actions_[idx].toObject().value("joints").toArray();
    out.reserve(a.size());
    for (const auto &v : a)
        out.push_back(v.toDouble());
    return out;
}

void ActionStore::setActionName(int idx, const QString &name)
{
    if (idx < 0 || idx >= actions_.size())
        return;
    auto obj = actions_[idx].toObject();
    obj.insert("name", name);
    actions_[idx] = obj;
}

void ActionStore::setActionJoints(int idx, const QVector<double> &joints)
{
    if (idx < 0 || idx >= actions_.size())
        return;
    QJsonArray arr;
    for (double v : joints)
        arr.append(QJsonValue(v));
    auto obj = actions_[idx].toObject();
    obj.insert("joints", arr);
    actions_[idx] = obj;
}

void ActionStore::addAction(const QString &name, const QVector<double> &joints)
{
    QJsonObject obj;
    obj.insert("name", name);
    QJsonArray arr;
    for (double v : joints)
        arr.append(v);
    obj.insert("joints", arr);
    actions_.append(obj);
}

void ActionStore::removeAction(int idx)
{
    if (idx < 0 || idx >= actions_.size())
        return;
    actions_.removeAt(idx);
}

int ActionStore::findActionByName(const QString &name) const
{
    for (int i = 0; i < actions_.size(); ++i)
    {
        if (actions_[i].toObject().value("name").toString() == name)
            return i;
    }
    return -1;
}

// -------- Action Sets --------
int ActionStore::actionSetCount() const { return actionSets_.size(); }

QString ActionStore::actionSetName(int idx) const
{
    if (idx < 0 || idx >= actionSets_.size())
        return {};
    return actionSets_[idx].toObject().value("name").toString();
}

QStringList ActionStore::actionSetList(int idx) const
{
    QStringList out;
    if (idx < 0 || idx >= actionSets_.size())
        return out;
    auto arr = actionSets_[idx].toObject().value("list").toArray();
    for (const auto &v : arr)
        out << v.toString();
    return out;
}
