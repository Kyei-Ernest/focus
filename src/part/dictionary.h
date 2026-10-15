/*
    SPDX-FileCopyrightText: 2026 Okular Custom Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <QString>
#include <QStringList>

// Offline English dictionary (Webster's 1913, public domain) backed by a
// read-only SQLite database shipped with the application.
class Dictionary
{
public:
    // Definition for word, or empty string when unknown or unavailable.
    static QString define(const QString &word);
    static bool isAvailable();

private:
    static QString normalized(const QString &word);
    static QStringList inflectionCandidates(const QString &lowercaseKey);
};

#endif // DICTIONARY_H
