/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "editqueue.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace OCC::Nourax::Agenda {

EditQueue::EditQueue(const QString &directory)
    : _directory(directory)
{
}

void EditQueue::load()
{
    _commands.clear();
    _conflicts.clear();
    QFile file(QDir(_directory).filePath(fileName()));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || doc.object().value(QLatin1String("cache_schema")).toInt() != schema) {
        return;
    }
    const auto o = doc.object();
    for (const auto &v : o.value(QLatin1String("commands")).toArray()) {
        auto c = EditCommand::fromJson(v.toObject());
        if (!c.id.isEmpty()) {
            c.state = CommandState::Waiting;
            _commands.append(c);
        }
    }
    for (const auto &v : o.value(QLatin1String("conflicts")).toArray()) {
        auto c = EditCommand::fromJson(v.toObject());
        if (!c.id.isEmpty()) {
            c.state = CommandState::Conflict;
            _conflicts.append(c);
        }
    }
}

bool EditQueue::save() const
{
    QDir().mkpath(_directory);
    QJsonArray commands;
    for (const auto &c : _commands) {
        commands.append(c.toJson());
    }
    QJsonArray conflicts;
    for (const auto &c : _conflicts) {
        conflicts.append(c.toJson());
    }
    QJsonObject o;
    o.insert(QStringLiteral("cache_schema"), schema);
    o.insert(QStringLiteral("commands"), commands);
    o.insert(QStringLiteral("conflicts"), conflicts);
    QSaveFile file(QDir(_directory).filePath(fileName()));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return file.commit();
}

void EditQueue::removeFile() const
{
    QFile::remove(QDir(_directory).filePath(fileName()));
}

QString EditQueue::enqueue(const EditCommand &command)
{
    for (auto &c : _commands) {
        if (c.id != _sendingId && c.canMerge(command)) {
            c.merge(command);
            return c.id;
        }
    }
    auto c = command;
    c.state = CommandState::Waiting;
    _commands.append(c);
    return c.id;
}

const EditCommand *EditQueue::find(const QString &id) const
{
    for (const auto &c : _commands) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

const EditCommand *EditQueue::findConflict(const QString &id) const
{
    for (const auto &c : _conflicts) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

bool EditQueue::markPartial(const QString &id)
{
    for (auto &c : _commands) {
        if (c.id == id) {
            c.change.insert(QStringLiteral("partial"), true);
            return true;
        }
    }
    return false;
}

const EditCommand *EditQueue::next() const
{
    return _commands.isEmpty() ? nullptr : &_commands.first();
}

void EditQueue::markAttempt(const QString &id)
{
    for (auto &c : _commands) {
        if (c.id == id) {
            ++c.attempts;
        }
    }
}

bool EditQueue::remove(const QString &id)
{
    for (int i = 0; i < _commands.size(); ++i) {
        if (_commands.at(i).id == id) {
            _commands.removeAt(i);
            return true;
        }
    }
    return false;
}

bool EditQueue::moveToConflict(const QString &id, const QString &message, const QString &conflictKind)
{
    for (int i = 0; i < _commands.size(); ++i) {
        if (_commands.at(i).id == id) {
            auto c = _commands.takeAt(i);
            c.state = CommandState::Conflict;
            c.message = message;
            c.conflictKind = conflictKind;
            _conflicts.append(c);
            return true;
        }
    }
    return false;
}

bool EditQueue::reopen(const QString &id, const QString &newBaseVersion)
{
    for (int i = 0; i < _conflicts.size(); ++i) {
        if (_conflicts.at(i).id == id) {
            auto c = _conflicts.takeAt(i);
            c.state = CommandState::Waiting;
            c.attempts = 0;
            c.message.clear();
            c.conflictKind.clear();
            c.baseVersion = newBaseVersion;
            _commands.append(c);
            return true;
        }
    }
    return false;
}

bool EditQueue::dropConflict(const QString &id)
{
    for (int i = 0; i < _conflicts.size(); ++i) {
        if (_conflicts.at(i).id == id) {
            _conflicts.removeAt(i);
            return true;
        }
    }
    return false;
}

void EditQueue::rebase(const QString &itemKey, const QString &oldBase, const QString &newBase)
{
    if (newBase.isEmpty()) {
        return;
    }
    for (auto &c : _commands) {
        if (c.itemKey == itemKey && c.baseVersion == oldBase) {
            c.baseVersion = newBase;
        }
    }
}

} // namespace OCC::Nourax::Agenda
