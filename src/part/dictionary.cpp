/*
    SPDX-FileCopyrightText: 2026 Okular Custom Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dictionary.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>

namespace
{
QMutex &dictionaryMutex()
{
    static QMutex mutex;
    return mutex;
}

// Lazily opened, read-only, shared connection.
QSqlDatabase dictionaryDatabase()
{
    const QString connectionName = QStringLiteral("okular-dictionary");
    if (QSqlDatabase::contains(connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(connectionName);
            if (db.isOpen()) {
                return db;
            }
        }
        // Stale (closed) connection: drop it and re-establish below instead
        // of returning an invalid handle forever.
        QSqlDatabase::removeDatabase(connectionName);
    }
    const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("okular/dictionary.sqlite"));
    if (path.isEmpty()) {
        return QSqlDatabase();
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY=1"));
    if (!db.open()) {
        QSqlDatabase::removeDatabase(connectionName);
        return QSqlDatabase();
    }
    return db;
}
} // namespace

QString Dictionary::normalized(const QString &word)
{
    // First whitespace-separated token, stripped of surrounding punctuation.
    const QString token = word.simplified().split(QChar::fromLatin1(' '), Qt::SkipEmptyParts).value(0);
    int start = 0;
    while (start < token.size() && !token.at(start).isLetterOrNumber()) {
        ++start;
    }
    int end = token.size();
    while (end > start && !token.at(end - 1).isLetterOrNumber() && token.at(end - 1) != QChar::fromLatin1('\'')) {
        --end;
    }
    return token.mid(start, end - start);
}

QString Dictionary::define(const QString &word)
{
    const QString key = normalized(word);
    if (key.isEmpty()) {
        return QString();
    }
    QMutexLocker locker(&dictionaryMutex());
    QSqlDatabase db = dictionaryDatabase();
    if (!db.isOpen()) {
        return QString();
    }
    QStringList keys{key};
    keys += inflectionCandidates(key.toLower());
    keys.removeDuplicates();
    QSqlQuery query(db);
    query.setForwardOnly(true);
    // Column collation is NOCASE, so every candidate matches any case.
    for (const QString &k : std::as_const(keys)) {
        if (k.isEmpty()) {
            continue;
        }
        query.prepare(QStringLiteral("SELECT definition FROM entries WHERE word = ? LIMIT 1"));
        query.addBindValue(k);
        if (query.exec() && query.next()) {
            return query.value(0).toString().trimmed();
        }
    }
    return QString();
}

// Headword candidates for an inflected token, most likely first. Only
// reached when the exact form missed, so over-generation is harmless.
QStringList Dictionary::inflectionCandidates(const QString &lower)
{
    QStringList out;
    const auto push = [&out](const QString &s) {
        if (s.size() > 1) {
            out.append(s);
        }
    };
    // Possessives: dog's -> dog, dogs' -> dogs (+ its own inflections).
    if (lower.endsWith(QLatin1String("'s")) && lower.size() > 2) {
        const QString stem = lower.left(lower.size() - 2);
        push(stem);
        for (const QString &sub : inflectionCandidates(stem)) {
            push(sub);
        }
    }
    if (lower.endsWith(QLatin1String("s'")) && lower.size() > 2) {
        const QString stem = lower.left(lower.size() - 1);
        push(stem);
        for (const QString &sub : inflectionCandidates(stem)) {
            push(sub);
        }
    }
    // Irregular plurals.
    if (lower == QLatin1String("children")) {
        push(QStringLiteral("child"));
    } else if (lower == QLatin1String("men")) {
        push(QStringLiteral("man"));
    } else if (lower == QLatin1String("women")) {
        push(QStringLiteral("woman"));
    } else if (lower == QLatin1String("feet")) {
        push(QStringLiteral("foot"));
    } else if (lower == QLatin1String("teeth")) {
        push(QStringLiteral("tooth"));
    } else if (lower == QLatin1String("mice")) {
        push(QStringLiteral("mouse"));
    } else if (lower == QLatin1String("geese")) {
        push(QStringLiteral("goose"));
    } else if (lower == QLatin1String("oxen")) {
        push(QStringLiteral("ox"));
    }
    // Plurals: tries -> try, wolves -> wolf/wife, churches -> church,
    // dogs -> dog.
    if (lower.endsWith(QLatin1String("ies")) && lower.size() > 4) {
        push(lower.left(lower.size() - 3) + QLatin1String("y"));
    }
    if (lower.endsWith(QLatin1String("ves")) && lower.size() > 4) {
        const QString stem = lower.left(lower.size() - 3);
        push(stem + QLatin1String("f"));
        push(stem + QLatin1String("fe"));
    }
    if (lower.size() > 4
        && (lower.endsWith(QLatin1String("ses")) || lower.endsWith(QLatin1String("xes")) || lower.endsWith(QLatin1String("zes"))
            || lower.endsWith(QLatin1String("ches")) || lower.endsWith(QLatin1String("shes")))) {
        push(lower.left(lower.size() - 2));
    }
    if (lower.endsWith(QLatin1Char('s')) && lower.size() > 3 && !lower.endsWith(QLatin1String("ss"))) {
        push(lower.left(lower.size() - 1));
    }
    // Past/participle: tried -> try, walked -> walk, loved -> love,
    // stopped -> stop.
    if (lower.endsWith(QLatin1String("ied")) && lower.size() > 4) {
        push(lower.left(lower.size() - 3) + QLatin1String("y"));
    }
    if (lower.endsWith(QLatin1String("ed")) && lower.size() > 4) {
        const QString stem = lower.left(lower.size() - 2);
        push(stem);
        push(stem + QLatin1String("e"));
        const int n = stem.size();
        if (n > 2 && stem.at(n - 1) == stem.at(n - 2) && QStringLiteral("aeiou").indexOf(stem.at(n - 1)) < 0) {
            push(stem.left(n - 1));
        }
    }
    // Gerund: walking -> walk, loving -> love, running -> run, dying -> die.
    if (lower.endsWith(QLatin1String("ying")) && lower.size() > 5) {
        push(lower.left(lower.size() - 4) + QLatin1String("ie"));
    }
    if (lower.endsWith(QLatin1String("ing")) && lower.size() > 5) {
        const QString stem = lower.left(lower.size() - 3);
        push(stem);
        push(stem + QLatin1String("e"));
        const int n = stem.size();
        if (n > 2 && stem.at(n - 1) == stem.at(n - 2) && QStringLiteral("aeiou").indexOf(stem.at(n - 1)) < 0) {
            push(stem.left(n - 1));
        }
    }
    return out;
}

bool Dictionary::isAvailable()
{
    QMutexLocker locker(&dictionaryMutex());
    return dictionaryDatabase().isOpen();
}
