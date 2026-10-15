/*
    SPDX-FileCopyrightText: 2010 Aleix Pol <aleixpol@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aboutdata.h"

#include <KCrash>
#include <KLocalizedContext>

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTimer>

#ifdef __ANDROID__
#include "android.h"

Q_DECL_EXPORT
#endif
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("focuskirigami"));

    KLocalizedString::setApplicationDomain("org.kde.active.documentviewer");

    KAboutData aboutData = okularAboutData();
    KAboutData::setApplicationData(aboutData);

    QApplication::setDesktopFileName(QStringLiteral("org.kde.focus.kirigami"));
    QIcon mobileIcon = QIcon::fromTheme(QStringLiteral("focus"));
    if (!mobileIcon.isNull()) {
        QApplication::setWindowIcon(mobileIcon);
    }

    KCrash::initialize();

    QCommandLineParser parser;
    // parser.setApplicationDescription(i18n("Focus mobile"));
    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);
    QQmlApplicationEngine engine;

#ifdef __ANDROID__
    URIHandler::handleViewIntent();
    const QString uri = URIHandler::handler.m_lastUrl;
#else
    const QString uri = parser.positionalArguments().count() == 1 ? QUrl::fromUserInput(parser.positionalArguments().constFirst(), {}, QUrl::AssumeLocalFile).toString() : QString();
#endif
    // TODO move away from context property when possible
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("uri"), uri);

    engine.loadFromModule(QLatin1StringView("org.kde.focus.app"), QLatin1StringView("Main"));
    return app.exec();
}
