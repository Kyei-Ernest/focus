/*
    SPDX-FileCopyrightText: 2002 Wilco Greven <greven@kde.org>
    SPDX-FileCopyrightText: 2003 Christophe Devriese <Christophe.Devriese@student.kuleuven.ac.be>
    SPDX-FileCopyrightText: 2003 Laurent Montel <montel@kde.org>
    SPDX-FileCopyrightText: 2003-2007 Albert Astals Cid <aacid@kde.org>
    SPDX-FileCopyrightText: 2004 Andy Goossens <andygoossens@telenet.be>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "shell.h"

#include "aboutdata.h"
#include "okular_main.h"
#include "shellutils.h"
#include <KAboutData>
#include <KCrash>
#include <KIconTheme>
#include <KLocalizedString>
#include <KMessageBox>
#include <KWindowSystem>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QFileOpenEvent>
#include <QIcon>
#include <QObject>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QtGlobal>

#define HAVE_STYLE_MANAGER __has_include(<KStyleManager>)
#if HAVE_STYLE_MANAGER
#include <KStyleManager>
#endif

/**
 * Event handler for macOS file opening via QFileOpenEvent.
 * Should not do anything on Windows/Linux as QFileOpenEvent is never fired there.
 */

class FileOpenEventHandler : public QObject
{
    Q_OBJECT
protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            auto *foe = static_cast<QFileOpenEvent *>(event);
            // Find existing Shell window
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget *widget : widgets) {
                if (Shell *shell = qobject_cast<Shell *>(widget)) {
                    QString serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
                    shell->openDocument(foe->url(), serializedOptions);
                    shell->raise();
                    shell->activateWindow();
                    return true;
                }
            }
            return false;
        }
        return QObject::eventFilter(obj, event);
    }
};

int main(int argc, char **argv)
{
    /**
     * trigger initialisation of proper icon theme
     */
#if KICONTHEMES_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    KIconTheme::initTheme();
#endif

    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents);

    QApplication app(argc, argv);

    /**
     * Install event filter to handle macOS file opening.
     * This enables double-click and "Open With" on macOS.
     */
    FileOpenEventHandler eventHandler;
    app.installEventFilter(&eventHandler);
    KLocalizedString::setApplicationDomain("focus");

#if HAVE_STYLE_MANAGER
    /**
     * trigger initialisation of proper application style
     */
    KStyleManager::initStyle();
#else
    /**
     * For Windows and macOS: use Breeze if available
     * Of all tested styles that works the best for us
     */
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QApplication::setStyle(QStringLiteral("breeze"));
#endif
#endif

    // This fork uses a dark theme throughout: prefer light (breeze-dark)
    // icons so toolbar/sidebar icons stay visible. Set after the style init
    // above so it is not overridden. Guarded so native builds without
    // breeze-dark keep the default theme instead of blank icons.
    if (!QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("icons/breeze-dark/index.theme")).isEmpty()) {
        QIcon::setThemeName(QStringLiteral("breeze-dark"));
    }

    KAboutData aboutData = okularAboutData();
    KAboutData::setApplicationData(aboutData);
    // Wayland/dock association: match org.kde.focus.desktop (Icon=focus)
    QApplication::setDesktopFileName(QStringLiteral("org.kde.focus"));
    // set icon for shells which do not use desktop file metadata.
    // Theme lookup first, absolute/XDG fallbacks so the reticle shows
    // even when the icon cache is stale.
    QIcon windowIcon = QIcon::fromTheme(QStringLiteral("focus"));
    if (windowIcon.isNull()) {
        const QStringList candidates = {
            QStringLiteral("/app/share/icons/hicolor/128-apps-focus.png"),
            QStringLiteral("/app/share/icons/hicolor/128x128/apps/focus.png"),
            QStringLiteral("/app/share/icons/hicolor/64x64/apps/focus.png"),
            QStringLiteral("/app/share/icons/hicolor/48x48/apps/focus.png"),
            QStringLiteral("/app/share/icons/hicolor/32x32/apps/focus.png"),
        };
        for (const QString &path : candidates) {
            if (QFile::exists(path)) {
                windowIcon = QIcon(path);
                break;
            }
        }
    }
    if (windowIcon.isNull()) {
        const QString located = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                       QStringLiteral("icons/hicolor/128x128/apps/focus.png"));
        if (!located.isEmpty()) {
            windowIcon = QIcon(located);
        }
    }
    if (!windowIcon.isNull()) {
        QApplication::setWindowIcon(windowIcon);
    }

    KCrash::initialize();

    QCommandLineParser parser;
    // The KDE4 version accepted flags such as -unique with a single dash -> preserve compatibility
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    aboutData.setupCommandLine(&parser);

    parser.addOption(QCommandLineOption({QStringLiteral("p"), QStringLiteral("page")}, i18n("Page of the document to be shown"), QStringLiteral("number")));
    parser.addOption(QCommandLineOption({QStringLiteral("presentation")}, i18n("Start the document in presentation mode")));
    parser.addOption(QCommandLineOption({QStringLiteral("print")}, i18n("Start with print dialog")));
    parser.addOption(QCommandLineOption({QStringLiteral("print-and-exit")}, i18n("Start with print dialog and exit after printing")));
    parser.addOption(QCommandLineOption({QStringLiteral("unique")}, i18n("\"Unique instance\" control")));
    parser.addOption(QCommandLineOption({QStringLiteral("noraise")}, i18n("Not raise window")));
    parser.addOption(QCommandLineOption({QStringLiteral("find")}, i18n("Find a string on the text"), QStringLiteral("string")));
    parser.addOption(QCommandLineOption({QStringLiteral("editor-cmd")}, i18n("Sets the external editor command"), QStringLiteral("string")));
    parser.addPositionalArgument(QStringLiteral("urls"), i18n("Documents to open. Specify '-' to read from stdin."));

    parser.process(app);
    aboutData.processCommandLine(&parser);

    // see if we are starting with session management
    if (app.isSessionRestored()) {
        kRestoreMainWindows<Shell>();
    } else {
        // no session.. just start up normally
        QStringList paths;
        for (int i = 0; i < parser.positionalArguments().count(); ++i) {
            paths << parser.positionalArguments().at(i);
        }
        Okular::Status status = Okular::main(paths, ShellUtils::serializeOptions(parser));
        switch (status) {
        case Okular::Error:
            return -1;
        case Okular::AttachedOtherProcess:
            return 0;
        case Okular::Success:
            // Do nothing
            break;
        }
    }

    return app.exec();
}
#include "main.moc"
