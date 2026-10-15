/*
    SPDX-FileCopyrightText: 2002 Wilco Greven <greven@kde.org>
    SPDX-FileCopyrightText: 2002 Chris Cheney <ccheney@cheney.cx>
    SPDX-FileCopyrightText: 2003 Benjamin Meyer <benjamin@csh.rit.edu>
    SPDX-FileCopyrightText: 2003-2004 Christophe Devriese <Christophe.Devriese@student.kuleuven.ac.be>
    SPDX-FileCopyrightText: 2003 Laurent Montel <montel@kde.org>
    SPDX-FileCopyrightText: 2003-2004 Albert Astals Cid <aacid@kde.org>
    SPDX-FileCopyrightText: 2003 Luboš Luňák <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2003 Malcolm Hunter <malcolm.hunter@gmx.co.uk>
    SPDX-FileCopyrightText: 2004 Dominique Devriese <devriese@kde.org>
    SPDX-FileCopyrightText: 2004 Dirk Mueller <mueller@kde.org>

    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2017 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "shell.h"

// qt/kde includes
#include <KActionCollection>
#include <KConfigGroup>
#include <KIO/Global>
#include <KLocalizedString>
#include <KMessageBox>
#include <KPluginFactory>
#include <KRecentFilesAction>
#include <KSharedConfig>
#include <KStandardAction>
#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
#include <KStartupInfo>
#include <KWindowInfo>
#endif
#include <KToggleFullScreenAction>
#include <KToolBar>
#include <KUrlMimeData>
#include <KWindowSystem>
#include <KXMLGUIFactory>
#include <QApplication>
#if HAVE_DBUS
#include <QDBusConnection>
#endif // HAVE_DBUS
#include <QDebug>
#include <QDockWidget>
#include <QDragMoveEvent>
#include <QChildEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QMenuBar>
#include <QMimeData>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

// local includes
#include "../interfaces/viewerinterface.h"
#include "kdocumentviewer.h"
#include "shellutils.h"

static const char *shouldShowMenuBarComingFromFullScreen = "shouldShowMenuBarComingFromFullScreen";
static const char *shouldShowToolBarComingFromFullScreen = "shouldShowToolBarComingFromFullScreen";

static const char *const SESSION_URL_KEY = "Urls";
static const char *const SESSION_TAB_KEY = "ActiveTab";

static constexpr char SIDEBAR_LOCKED_KEY[] = "LockSidebar";
static constexpr char SIDEBAR_VISIBLE_KEY[] = "ShowSidebar";

static inline QString DesktopEntryGroupKey()
{
    return QStringLiteral("Desktop Entry");
}
static inline QString RecentFilesGroupKey()
{
    return QStringLiteral("Recent Files");
}
static inline QString GeneralGroupKey()
{
    return QStringLiteral("General");
}
// Solid reader toolbar: opaque #101010 so the bar never shows the document
// through (it is pinned over the page). Matches the selected document tab.
static inline QString overlayToolbarStyle()
{
    return QStringLiteral(
        "QToolBar { background-color: #101010; border: none; spacing: 4px; }"
        "QToolBar QToolButton { background: transparent; border: none; color: #F2F0EB; }"
        "QToolBar QToolButton:hover { background: #24211D; border-radius: 6px; }"
        "QToolBar QToolButton:pressed { background: #39342D; border-radius: 6px; }"
        "QToolBar QToolButton:disabled { color: #898278; }"
        "QToolBar QLabel { color: #F2F0EB; background: transparent; }"
        "QToolBar::separator { background: #39342D; width: 1px; margin: 8px 4px; }");
}
// True when the pixmap carries real color (hue), not a monochrome glyph:
// colorful pixmaps (annotation color wheel, active-state amber) keep their
// original pixels so only dark glyphs get whitened for the dark bar.
static bool toolbarIconIsColor(const QPixmap &pm)
{
    if (pm.isNull()) {
        return false;
    }
    const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    int opaque = 0, colorful = 0;
    const int step = qMax(1, qMin(w, h) / 24); // sample, don't scan every pixel
    for (int y = 0; y < h; y += step) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; x += step) {
            const QRgb p = line[x];
            if (qAlpha(p) < 64) {
                continue;
            }
            ++opaque;
            const int mx = qMax(qRed(p), qMax(qGreen(p), qBlue(p)));
            const int mn = qMin(qRed(p), qMin(qGreen(p), qBlue(p)));
            if (mx - mn > 60) {
                ++colorful;
            }
            if (colorful > opaque / 8 + 4) {
                return true;
            }
        }
    }
    return opaque > 0 && colorful > opaque / 8;
}
// Recolor one pixmap's opaque pixels to ink, preserving per-pixel alpha.
static QPixmap toolbarIconToLight(const QPixmap &pm, const QColor &ink)
{
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a == 0) {
                continue;
            }
            line[x] = qRgba(ink.red(), ink.green(), ink.blue(), a);
        }
    }
    QPixmap out = QPixmap::fromImage(img);
    out.setDevicePixelRatio(pm.devicePixelRatio());
    return out;
}
// Whiten a toolbar button icon for the dark bar: monochrome glyphs become
// light ink (muted when disabled); genuinely colorful pixmaps are kept.
static QIcon lightToolbarIcon(const QIcon &src, const QSize &atSize)
{
    if (src.isNull()) {
        return src;
    }
    static const QColor s_ink(0xF2, 0xF0, 0xEB);
    static const QColor s_muted(0x89, 0x82, 0x78);
    QIcon out;
    bool any = false;
    const QList<QSize> sizes{atSize, atSize * 2};
    for (const QIcon::Mode mode : {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled}) {
        for (const QIcon::State state : {QIcon::Off, QIcon::On}) {
            for (const QSize &sz : sizes) {
                const QPixmap pm = src.pixmap(sz, mode, state);
                if (pm.isNull()) {
                    continue;
                }
                if (toolbarIconIsColor(pm)) {
                    out.addPixmap(pm, mode, state);
                } else {
                    out.addPixmap(toolbarIconToLight(pm, mode == QIcon::Disabled ? s_muted : s_ink), mode, state);
                }
                any = true;
            }
        }
    }
    return any ? out : src;
}

class ResizableStackedWidget : public QStackedWidget
{
    Q_OBJECT

public:
    QSize sizeHint() const override
    {
        return currentWidget()->sizeHint();
    }
    QSize minimumSizeHint() const override
    {
        return currentWidget()->minimumSizeHint();
    }
};

/**
 * Groups sidebar containers in a QDockWidget.
 *
 * This control groups all the sidebar containers provided by each tab (the Part object),
 * allowing the user to dock it to the left and right sides of the window,
 * or detach it from the window altogether.
 */
class Sidebar : public QDockWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr)
        : QDockWidget(parent)
    {
        setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        setFeatures(defaultFeatures());

        m_stackedWidget = new QStackedWidget;
        setWidget(m_stackedWidget);
        // It seems that without requesting a specific minimum size, Qt
        // somehow calculates a (0,-1) minimum size, and then Qt gets angry
        // that negative sizes is not possible.
        setMinimumSize(10, 10);
    }

    bool isLocked() const
    {
        return features().testFlag(NoDockWidgetFeatures);
    }

    void setLocked(bool locked)
    {
        setFeatures(locked ? NoDockWidgetFeatures : defaultFeatures());

        // show titlebar only if not locked
        if (locked) {
            if (!m_dumbTitleWidget) {
                m_dumbTitleWidget = new QWidget;
            }
            setTitleBarWidget(m_dumbTitleWidget);
        } else {
            setTitleBarWidget(nullptr);
        }
    }

    int indexOf(QWidget *widget) const
    {
        return m_stackedWidget->indexOf(widget);
    }

    void addWidget(QWidget *widget)
    {
        m_stackedWidget->addWidget(widget);
    }

    void removeWidget(QWidget *widget)
    {
        m_stackedWidget->removeWidget(widget);
    }

    void setCurrentWidget(QWidget *widget)
    {
        m_stackedWidget->setCurrentWidget(widget);
    }

private:
    static DockWidgetFeatures defaultFeatures()
    {
        DockWidgetFeatures dockFeatures = DockWidgetClosable | DockWidgetMovable;
        if (!KWindowSystem::isPlatformWayland()) { // TODO : Remove this check when QTBUG-87332 is fixed
            dockFeatures |= DockWidgetFloatable;
        }

        return dockFeatures;
    }

    QStackedWidget *m_stackedWidget = nullptr;
    QWidget *m_dumbTitleWidget = nullptr;
};

Shell::Shell(const QString &serializedOptions)
    : KParts::MainWindow()
    , m_menuBarWasShown(true)
    , m_toolBarWasShown(true)
    , m_isValid(true)
{
    setObjectName(QStringLiteral("focus::Shell#"));
    setContextMenuPolicy(Qt::NoContextMenu);
    // Build marker: identifies exactly which custom build is running
    // (visible via `make logs`). Uses compile date/time, no maintenance.
    qInfo("focus-custom build %s %s", __DATE__, __TIME__);
    {
        // Same marker as a file: docker logs stay empty in this setup.
        QFile tag(QStringLiteral("/tmp/focus-build-tag.txt"));
        if (tag.open(QIODevice::WriteOnly | QIODevice::Text)) {
            tag.write(__DATE__);
            tag.write(" ", 1);
            tag.write(__TIME__);
            tag.write("\n", 1);
        }
    }
    // otherwise .rc file won't be found by unit test
    setComponentName(QStringLiteral("focus"), QString());
    // set the shell's ui resource file
    setXMLFile(QStringLiteral("shell.rc"));
    m_fileformatsscanned = false;
    m_showMenuBarAction = nullptr;
    // this routine will find and load our Part.  it finds the Part by
    // name which is a bad idea usually.. but it's alright in this
    // case since our Part is made for this Shell

    const auto result = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf6/parts/focuspart")));

    if (!result) {
        // if we couldn't find our Part, we exit since the Shell by
        // itself can't do anything useful
        m_isValid = false;
        KMessageBox::error(this, i18n("Unable to find the Focus component: %1", result.errorString));
        return;
    } else {
        m_partFactory = result.plugin;
    }

    // now that the Part plugin is loaded, create the part
    KParts::ReadWritePart *const firstPart = m_partFactory->create<KParts::ReadWritePart>(this);
    if (firstPart) {
        // Setup the central widget
        m_centralStackedWidget = new ResizableStackedWidget();
        setCentralWidget(m_centralStackedWidget);

        // Setup tab bar. Tab 0 is always the pinned Library tab, so the
        // tab bar stays visible at all times.
        m_tabWidget = new QTabWidget(this);
        m_tabWidget->setTabsClosable(true);
        m_tabWidget->setElideMode(Qt::ElideRight);
        m_tabWidget->setDocumentMode(true);
        m_tabWidget->setMovable(true);
        // Warm-dark tab strip to match the Library page; the Library tab
        // itself is styled major (wide + amber when selected). Document
        // tabs stay dark with an amber underline when selected, so the
        // black Focus toolbar below them reads as one clean bar.
        m_tabWidget->setStyleSheet(QStringLiteral(
            "QTabWidget::pane { background: #1E1A15; border: none; }"
            "QTabBar::tab { background: #3D362D; color: #D9CFC0; padding: 10px 22px; margin: 4px 3px 0 3px;"
            " border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: bold; font-size: 13px; min-width: 120px; }"
            "QTabBar::tab:hover:!selected { background: #4C4438; color: #FFFFFF; }"
            "QTabBar::tab:selected { background: #101010; color: #FFC15A; border-top: 3px solid #E8A33D; }"
            "QTabBar::tab:first:selected { background: #E8A33D; color: #1E1A15; border-top: none; }"
            "QTabBar::tab:first { min-width: 190px; padding: 12px 26px; font-size: 14px; }"));

        m_tabWidget->setAcceptDrops(true);
        m_tabWidget->tabBar()->installEventFilter(this);
        // Focus design has no tab strip: the toolbar (back button + title)
        // is the only top chrome. Tab switching stays available via the
        // Next/Previous Tab shortcuts; closing via Ctrl+W.
        m_tabWidget->tabBar()->hide();

        m_centralStackedWidget->addWidget(m_tabWidget);

        // Setup the library tab (reuses the welcome-screen widget).
        m_welcomeScreen = new WelcomeScreen(m_tabWidget);
        connect(m_welcomeScreen, &WelcomeScreen::openClicked, this, &Shell::fileOpen);
        connect(m_welcomeScreen, &WelcomeScreen::closeClicked, this, &Shell::hideWelcomeScreen);
        connect(m_welcomeScreen, &WelcomeScreen::recentItemClicked, this, [this](const QUrl &url) { openUrl(url); });
        connect(m_welcomeScreen, &WelcomeScreen::forgetRecentItem, this, &Shell::forgetRecentItem);
        m_tabWidget->insertTab(0, m_welcomeScreen, i18n("Library"));

        m_welcomeScreen->installEventFilter(this);

        connect(m_tabWidget, &QTabWidget::currentChanged, this, &Shell::setActiveTab);
        connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &Shell::closeTab);
        connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, &Shell::moveTabData);

        m_sidebar = new Sidebar;
        m_sidebar->setObjectName(QStringLiteral("focus_sidebar"));
        m_sidebar->setContextMenuPolicy(Qt::ActionsContextMenu);
        m_sidebar->setWindowTitle(i18n("Sidebar"));
        connect(m_sidebar, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            // sync sidebar visibility with the m_showSidebarAction only if welcome screen is hidden
            if (m_showSidebarAction && m_centralStackedWidget->currentWidget() != m_welcomeScreen) {
                m_showSidebarAction->setChecked(visible);
            }
            if (m_centralStackedWidget->currentWidget() == m_welcomeScreen) {
                // MainWindow tries hard to make its child dockwidgets shown, but during
                // welcome screen we don't want to see the sidebar,
                // so try a bit more to actually hide it.
                m_sidebar->hide();
            }
        });
        addDockWidget(Qt::LeftDockWidgetArea, m_sidebar);

        // then, setup our actions
        setupActions();
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &QObject::deleteLater);
        // and integrate the part's GUI with the shell's
        setupGUI(Keys | ToolBar | Save);

        // NOTE : apply default sidebar width only after calling setupGUI(...)
        resizeDocks({m_sidebar}, {200}, Qt::Horizontal);

        // The document toolbar lives as a slim bar inside the
        // document area (on top of the open document) instead of a docked
        // row: undock it once and parent it to the tab widget. setActiveTab()
        // shows it on document tabs and hides it on the Library tab.
        // Solid opaque bar (#101010, one 20px icon size, ~40px high,
        // everything vertically centered). The part toolbar definition
        // (part.rc) provides the expanding spacer between this left zone
        // and the right-side controls, plus separators between logical
        // groups only.
        removeToolBar(toolBar());
        toolBar()->setParent(m_tabWidget);
        toolBar()->setMovable(false);
        toolBar()->setFloatable(false);
        toolBar()->setIconSize(QSize(20, 20));
        toolBar()->setToolButtonStyle(Qt::ToolButtonIconOnly);
        toolBar()->setFixedHeight(40);
        toolBar()->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toolBar()->setAutoFillBackground(true);
        toolBar()->setStyleSheet(overlayToolbarStyle());
        // Code-owned bar controls (never recreated by GUI merges): back to
        // Library, document title, fullscreen toggle. Plain addWidget (NOT
        // QWidgetAction): the action-wrapper path hangs the constructor here.
        auto *backButton = new QToolButton(toolBar());
        backButton->setDefaultAction(actionCollection()->action(QStringLiteral("back_to_library")));
        backButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        backButton->setAutoRaise(true);
        toolBar()->addWidget(backButton);
        m_backToolbarAction = toolBar()->actions().isEmpty() ? nullptr : toolBar()->actions().last();
        m_docTitle = new QLabel(toolBar());
        // QLabel has no setTextElideMode: elide manually in
        // updateDocTitleElide() so a long title can never push other
        // controls around.
        m_docTitle->setWordWrap(false);
        m_docTitle->setMinimumWidth(0);
        m_docTitle->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
        m_docTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_docTitle->installEventFilter(this);
        toolBar()->addWidget(m_docTitle);
        m_titleToolbarAction = toolBar()->actions().isEmpty() ? nullptr : toolBar()->actions().last();
        auto *fullscreenButton = new QToolButton(toolBar());
        fullscreenButton->setDefaultAction(m_fullScreenAction);
        fullscreenButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        fullscreenButton->setAutoRaise(true);
        toolBar()->addWidget(fullscreenButton);
        m_fullscreenToolbarAction = toolBar()->actions().isEmpty() ? nullptr : toolBar()->actions().last();
        toolBar()->hide();
        m_tabWidget->installEventFilter(this);
        toolBar()->installEventFilter(this);

        m_tabs.append(TabState(firstPart));
        m_tabWidget->addTab(firstPart->widget(), QString());
        // The initial document tab stays hidden until its first document opens.
        m_tabWidget->setTabVisible(1, false);

        connectPart(firstPart);

        // Wire the first document tab so part actions exist, then read settings.
        setActiveTab(1);
        readSettings();

        m_unique = ShellUtils::unique(serializedOptions);
#if HAVE_DBUS
        if (m_unique) {
            m_unique = QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.focus"));
            if (!m_unique) {
                KMessageBox::information(this, i18n("There is already a unique Focus instance running. This instance won't be the unique one."));
            }
        } else {
            // TODO When porting to KF7 Remove
            // PID is not unique in containers and "-" in the name violates D-Bus naming conventions.
            // Was left for compatibility with 3rd-party scripts.
            QString serviceName = QStringLiteral("org.kde.focus-") + QString::number(qApp->applicationPid());
            QDBusConnection::sessionBus().registerService(serviceName);

            QDBusConnection::sessionBus().registerService(ShellUtils::currentProcessDbusName());
        }
        if (ShellUtils::noRaise(serializedOptions)) {
            setAttribute(Qt::WA_ShowWithoutActivating);
        }

        {
            const QString editorCmd = ShellUtils::editorCmd(serializedOptions);
            if (!editorCmd.isEmpty()) {
                QMetaObject::invokeMethod(firstPart, "setEditorCmd", Q_ARG(QString, editorCmd));
            }
        }

        QDBusConnection::sessionBus().registerObject(QStringLiteral("/focusshell"), this, QDBusConnection::ExportScriptableSlots);
#endif // HAVE_DBUS

        // Start on the pinned Library tab.
        pinLibraryTab();
        setActiveTab(0);
    } else {
        m_isValid = false;
        KMessageBox::error(this, i18n("Unable to find the Focus component."));
    }

    connect(guiFactory(), &KXMLGUIFactory::shortcutsSaved, this, &Shell::reloadAllXML);
}

void Shell::reloadAllXML()
{
    for (const TabState &tab : std::as_const(m_tabs)) {
        tab.part->reloadXML();
    }
}

void Shell::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && window()->isFullScreen()) {
        setFullScreen(false);
    }
}

bool Shell::eventFilter(QObject *obj, QEvent *event)
{
    // During teardown, child widgets may be half-destroyed: never touch them.
    if (m_shuttingDown) {
        return KParts::MainWindow::eventFilter(obj, event);
    }

    QDragMoveEvent *dmEvent = dynamic_cast<QDragMoveEvent *>(event);
    if (dmEvent) {
        bool accept = dmEvent->mimeData()->hasUrls();
        event->setAccepted(accept);
        return accept;
    }

    QDropEvent *dEvent = dynamic_cast<QDropEvent *>(event);
    if (dEvent) {
        const QList<QUrl> list = KUrlMimeData::urlsFromMimeData(dEvent->mimeData());
        handleDroppedUrls(list);
        dEvent->setAccepted(true);
        return true;
    }

    // Handle middle button click events on the tab bar
    if (obj == m_tabWidget->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mEvent = static_cast<QMouseEvent *>(event);
        if (mEvent->button() == Qt::MiddleButton) {
            int tabIndex = m_tabWidget->tabBar()->tabAt(mEvent->pos());
            if (tabIndex != -1) {
                closeTab(tabIndex);
                return true;
            }
        }
    }

    // Keep the overlay toolbar pinned over the document area on resizes,
    // restyling for the new width (responsive label/icon tiers).
    if (obj == m_tabWidget && event->type() == QEvent::Resize) {
        positionOverlayToolbar();
        if (toolBar() && !toolBar()->isHidden()) {
            styleOverlayButtons();
        }
    }

    // Re-elide the document title when its width changes.
    if (obj == m_docTitle && event->type() == QEvent::Resize) {
        updateDocTitleElide();
    }

    // Style overlay buttons at creation: merges (including ones finishing
    // asynchronously after tab switches) build fresh buttons at any time,
    // so point-in-time passes alone cannot cover them. Guarded: styling
    // itself reparents toolbar children (order fixes), which emits
    // ChildAdded — without the guard this recurses until stack overflow.
    if (obj == toolBar() && event->type() == QEvent::ChildAdded) {
        if (!m_stylingOverlay && qobject_cast<QToolButton *>(static_cast<QChildEvent *>(event)->child())) {
            if (!toolBar()->isHidden()) {
                styleOverlayButtons();
            }
        }
    }
    return KParts::MainWindow::eventFilter(obj, event);
}

bool Shell::isValid() const
{
    return m_isValid;
}

void Shell::showOpenRecentMenu()
{
    m_recent->menu()->popup(QCursor::pos());
}

Shell::~Shell()
{
    m_shuttingDown = true;
    if (!m_tabs.empty()) {
        writeSettings();
        for (const TabState &tab : std::as_const(m_tabs)) {
            tab.part->closeUrl(false);
        }
        m_tabs.clear();
    }
#if HAVE_DBUS
    if (m_unique) {
        QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.focus"));
    }
#endif // HAVE_DBUS

    delete m_tabWidget;
}

// Open a new document if we have space for it
// This can hang if called on a unique instance and openUrl pops a messageBox
bool Shell::openDocument(const QUrl &url, const QString &serializedOptions)
{
    if (m_tabs.size() <= 0) {
        return false;
    }

    hideWelcomeScreen();

    KParts::ReadWritePart *const part = m_tabs[0].part;

    // Return false if we can't open new tabs and the only part is occupied
    if (!qobject_cast<Okular::ViewerInterface *>(part)->openNewFilesInTabs() && !part->url().isEmpty() && !ShellUtils::unique(serializedOptions)) {
        return false;
    }

    openUrl(url, serializedOptions);

    return true;
}

bool Shell::openDocument(const QString &urlString, const QString &serializedOptions)
{
    return openDocument(QUrl(urlString), serializedOptions);
}

void Shell::openNewlySignedFile(const QString &path, int pageNumber)
{
    // for now, this function just applies the "replace current document"
    // strategy for opening.
    // Given signing is slightly closer to annotating and saving, the over
    // all user experience should not be that different
    // The fact that we get a different file out of saving is a bit of a
    // implementation  detail that shouldn't leak that much onto the users
    QUrl url = QUrl::fromLocalFile(path);
    url.setFragment(QStringLiteral("page=%1").arg(pageNumber));

    const int currentTab = m_tabWidget->currentIndex();
    const int doc = currentTab <= 0 ? qBound(0, m_lastDocTab - 1, m_tabs.size() - 1) : currentTab - 1;
    KParts::ReadWritePart *const activePart = m_tabs[doc].part;
    activePart->closeUrl(false);
    activePart->openUrl(url);
}

bool Shell::canOpenDocs(int numDocs, int desktop)
{
    if (m_tabs.size() <= 0 || numDocs <= 0 || m_unique) {
        return false;
    }

    KParts::ReadWritePart *const part = m_tabs[0].part;
    const bool allowTabs = qobject_cast<Okular::ViewerInterface *>(part)->openNewFilesInTabs();

    if (!allowTabs && (numDocs > 1 || !part->url().isEmpty())) {
        return false;
    }

#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
    const KWindowInfo winfo(window()->effectiveWinId(), NET::WMDesktop);
    if (winfo.desktop() != desktop) {
        return false;
    }
#else
    Q_UNUSED(desktop);
#endif

    return true;
}

void Shell::openUrl(const QUrl &url, const QString &serializedOptions)
{
    hideWelcomeScreen();

    const int current = m_tabWidget->currentIndex();
    if (current <= 0) {
        // Opened from the Library tab: reuse the initial empty document tab
        // if there is one, otherwise open a new tab.
        if (m_tabs.size() == 1 && m_tabs[0].part->url().isEmpty()) {
            reuseDocTab(0, url, serializedOptions);
        } else {
            openNewTab(url, serializedOptions);
        }
        return;
    }

    const int doc = current - 1;
    KParts::ReadWritePart *const activePart = m_tabs[doc].part;
    if (!activePart->url().isEmpty()) {
        if (m_unique) {
            applyOptionsToPart(activePart, serializedOptions);
            activePart->openUrl(url);
        } else {
            if (qobject_cast<Okular::ViewerInterface *>(activePart)->openNewFilesInTabs()) {
                openNewTab(url, serializedOptions);
            } else {
                Shell *newShell = new Shell(serializedOptions);
                newShell->show();
                newShell->openUrl(url, serializedOptions);
            }
        }
    } else {
        reuseDocTab(doc, url, serializedOptions);
    }
}

// Open a document into an existing, still-empty document tab.
void Shell::reuseDocTab(int docIdx, const QUrl &url, const QString &serializedOptions)
{
    const int tabIdx = docIdx + 1;
    m_tabWidget->setTabVisible(tabIdx, true);
    m_tabWidget->setTabText(tabIdx, url.fileName());
    m_tabWidget->setTabToolTip(tabIdx, url.fileName());

    KParts::ReadWritePart *const part = m_tabs[docIdx].part;
    applyOptionsToPart(part, serializedOptions);
    bool openOk = part->openUrl(url);
    const bool isstdin = url.fileName() == QLatin1String("-") || url.scheme() == QLatin1String("fd");
    if (!isstdin) {
        if (openOk) {
            m_recent->addUrl(url);
            m_welcomeScreen->noteDocumentOpened(url);
        } else {
            m_recent->removeUrl(url);
            closeTab(tabIdx);
            return;
        }
    }
    setActiveTab(tabIdx);
}

// Keep the Library widget pinned as tab 0 with its title and no close button.
void Shell::pinLibraryTab()
{
    if (m_tabWidget->indexOf(m_welcomeScreen) != 0) {
        m_tabWidget->blockSignals(true);
        const int at = m_tabWidget->indexOf(m_welcomeScreen);
        if (at > 0) {
            m_tabWidget->tabBar()->moveTab(at, 0);
        }
        m_tabWidget->blockSignals(false);
    }
    m_tabWidget->setTabText(0, i18n("Library"));
    m_tabWidget->setTabIcon(0, QIcon::fromTheme(QStringLiteral("book")));
    m_tabWidget->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
}

// Pin the (undocked) document toolbar over the open document. With the tab
// strip hidden it docks at the very top; otherwise just below the tabs.
void Shell::positionOverlayToolbar()
{
    QToolBar *tb = toolBar();
    if (!tb || !m_tabWidget || !m_tabWidget->tabBar()) {
        return;
    }
    const int y = m_tabWidget->tabBar()->isVisible() ? m_tabWidget->tabBar()->height() : 0;
    tb->setGeometry(0, y, m_tabWidget->width(), 40);
    tb->raise();
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    if (QLayout *lay = tb->layout()) {
        lay->invalidate();
        lay->activate();
    }
}

// Solid opaque bar: one icon size, icon-only buttons with tooltips from
// their actions. Enabled states stay owned by the actions themselves.
void Shell::styleOverlayButtons()
{
    QToolBar *tb = toolBar();
    if (!tb || m_stylingOverlay) {
        return;
    }
    m_stylingOverlay = true;
    struct StylingGuard {
        Shell *s;
        ~StylingGuard()
        {
            s->m_stylingOverlay = false;
        }
    } guard{this};

    // Solid opaque bar: one icon size, icon-only, keeps its background
    // across GUI merges (which reset styling on rebuilt buttons).
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(20, 20));
    tb->setAutoFillBackground(true);
    tb->setStyleSheet(overlayToolbarStyle());

    // Bury merge-orphan ghosts: buttons in the toolbar layout with no
    // action and no defaultAction serve nothing. The overflow extension
    // button (also action-less) is exempt and keeps its native look.
    if (QLayout *lay = tb->layout()) {
        for (QToolButton *btn : tb->findChildren<QToolButton *>()) {
            if (btn->parentWidget() != tb || btn->defaultAction()) {
                continue;
            }
            bool hasAction = false;
            for (QAction *a : tb->actions()) {
                if (tb->widgetForAction(a) == btn) {
                    hasAction = true;
                    break;
                }
            }
            if (!hasAction && lay->indexOf(btn) != -1 && btn->objectName() != QStringLiteral("qt_toolbar_ext_button")) {
                btn->hide();
            }
        }
    }
    // Fixed two-zone order: [back, title] lead (the part.rc Spacer after
    // them is the expanding spacer), fullscreen sits last. Only move
    // actions that are out of place: every remove/insert reparents toolbar
    // widgets and emits ChildAdded, so unconditional moves churn (guarded
    // above, but still).
    if (m_backToolbarAction && m_titleToolbarAction && tb->actions().size() >= 2) {
        const QList<QAction *> acts = tb->actions();
        if (acts.at(0) != m_backToolbarAction || acts.at(1) != m_titleToolbarAction) {
            QAction *first = acts.first();
            // Block toolbar action signals while reordering so no nested
            // ChildAdded styling pass starts mid-move.
            const bool wasBlocked = tb->blockSignals(true);
            tb->removeAction(m_backToolbarAction);
            tb->insertAction(first, m_backToolbarAction);
            tb->removeAction(m_titleToolbarAction);
            tb->insertAction(first, m_titleToolbarAction);
            tb->blockSignals(wasBlocked);
        }
    }
    if (m_fullscreenToolbarAction && !tb->actions().isEmpty()) {
        if (tb->actions().last() != m_fullscreenToolbarAction) {
            const bool wasBlocked = tb->blockSignals(true);
            tb->removeAction(m_fullscreenToolbarAction);
            tb->addAction(m_fullscreenToolbarAction);
            tb->blockSignals(wasBlocked);
        }
    }
    // Icon-only everywhere with tooltips from the action text. Enabled
    // states are left to the actions (only intentional disabled buttons).
    // Dark bar: whiten monochrome glyphs so they read on #101010 (color
    // icons keep their pixels). Button-level only, so menu copies of the
    // same actions keep their native icons.
    for (QAction *a : tb->actions()) {
        if (QToolButton *btn = qobject_cast<QToolButton *>(tb->widgetForAction(a))) {
            btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            btn->setAutoRaise(true);
            QString tip = a->text();
            tip.remove(QChar::fromLatin1('&'));
            if (!tip.isEmpty()) {
                btn->setToolTip(tip);
            }
            const QIcon src = a->icon().isNull() ? btn->icon() : a->icon();
            if (!src.isNull()) {
                btn->setIcon(lightToolbarIcon(src, tb->iconSize()));
            }
            btn->updateGeometry();
        }
    }
    // Custom container widgets in the bar are not action buttons: the
    // page-number MiniBar owns the previous/next page buttons, so whiten
    // their icons here for the dark bar (scoped to the toolbar only).
    for (QToolButton *btn : tb->findChildren<QToolButton *>()) {
        if (btn->parentWidget() == tb || btn->objectName() == QStringLiteral("qt_toolbar_ext_button")) {
            continue; // action buttons (handled above) and overflow button
        }
        const QIcon src = btn->icon();
        if (!src.isNull()) {
            const QSize atSize = btn->iconSize().isValid() ? btn->iconSize() : tb->iconSize();
            btn->setIcon(lightToolbarIcon(src, atSize));
        }
    }
    // Merges can leave the layout measuring stale geometries: force a
    // fresh pass.
    if (QLayout *lay = tb->layout()) {
        lay->invalidate();
        lay->activate();
    }
}

void Shell::closeUrl()
{
    closeTab(m_tabWidget->currentIndex());

    // When closing the current tab two things can happen:
    //  * the focus was on the tab
    //  * the focus was somewhere in the toolbar
    // we don't have other places that accept focus
    //  * If it was on the tab, logic says it should go back to the next current tab
    //  * If it was on the toolbar, we could leave it there, but since we redo the menus/toolbars for the new tab, it gets kind of lost
    //    so it's easier to set it to the next current tab which also makes sense as consistency
    if (m_tabWidget->count() >= 0) {
        const int current = m_tabWidget->currentIndex();
        if (current > 0) {
            KParts::ReadWritePart *const newPart = m_tabs[current - 1].part;
            newPart->widget()->setFocus();
        }
    }
}

void Shell::readSettings()
{
    readRecentFilesSettings();

    const KConfigGroup group = KSharedConfig::openConfig()->group(DesktopEntryGroupKey());
    bool fullScreen = group.readEntry("FullScreen", false);
    setFullScreen(fullScreen);

    if (fullScreen) {
        m_menuBarWasShown = group.readEntry(shouldShowMenuBarComingFromFullScreen, true);
        m_toolBarWasShown = group.readEntry(shouldShowToolBarComingFromFullScreen, true);
    }

    const KConfigGroup sidebarGroup = KSharedConfig::openConfig()->group(GeneralGroupKey());
    m_sidebar->setVisible(sidebarGroup.readEntry(SIDEBAR_VISIBLE_KEY, true));
    m_sidebar->setLocked(sidebarGroup.readEntry(SIDEBAR_LOCKED_KEY, true));

    m_showSidebarAction->setChecked(m_sidebar->isVisibleTo(this));
    m_lockSidebarAction->setChecked(m_sidebar->isLocked());
}

void Shell::writeSettings()
{
    saveRecents();

    KConfigGroup sidebarGroup = KSharedConfig::openConfig()->group(GeneralGroupKey());
    sidebarGroup.writeEntry(SIDEBAR_LOCKED_KEY, m_sidebar->isLocked());
    // NOTE : Consider whether the m_showSidebarAction is checked, because
    // the sidebar can be forcibly hidden if the welcome screen is displayed
    sidebarGroup.writeEntry(SIDEBAR_VISIBLE_KEY, m_sidebar->isVisibleTo(this) || m_showSidebarAction->isChecked());

    KConfigGroup group = KSharedConfig::openConfig()->group(DesktopEntryGroupKey());
    group.writeEntry("FullScreen", m_fullScreenAction->isChecked());
    if (m_fullScreenAction->isChecked()) {
        group.writeEntry(shouldShowMenuBarComingFromFullScreen, m_menuBarWasShown);
        group.writeEntry(shouldShowToolBarComingFromFullScreen, m_toolBarWasShown);
    }
    KSharedConfig::openConfig()->sync();
}

void Shell::saveRecents()
{
    m_recent->saveEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

void Shell::setupActions()
{
    KStandardAction::open(this, SLOT(fileOpen()), actionCollection());
    m_recent = KStandardAction::openRecent(this, SLOT(openUrl(QUrl)), actionCollection());
    m_recent->setToolBarMode(KRecentFilesAction::MenuMode);
    connect(m_recent, &QAction::triggered, this, &Shell::showOpenRecentMenu);
    connect(m_recent, &KRecentFilesAction::recentListCleared, this, &Shell::refreshRecentsOnWelcomeScreen);
    m_recent->setToolTip(i18n("Click to open a file\nClick and hold to open a recent file"));
    m_recent->setWhatsThis(i18n("<b>Click</b> to open a file or <b>Click and hold</b> to select a recent file"));
    m_printAction = KStandardAction::print(this, SLOT(print()), actionCollection());
    m_printAction->setEnabled(false);
    m_closeAction = KStandardAction::close(this, SLOT(closeUrl()), actionCollection());
    m_closeAction->setEnabled(false);
    KStandardAction::quit(this, SLOT(close()), actionCollection());

    setStandardToolBarMenuEnabled(true);

    m_showMenuBarAction = KStandardAction::showMenubar(this, SLOT(slotShowMenubar()), actionCollection());
    m_fullScreenAction = KStandardAction::fullScreen(this, SLOT(slotUpdateFullScreen()), this, actionCollection());

    actionCollection()->setDefaultShortcuts(m_fullScreenAction, KStandardShortcut::fullScreen() + QList {QKeySequence(Qt::Key_F11)});

    QAction *backToLibrary = actionCollection()->addAction(QStringLiteral("back_to_library"));
    backToLibrary->setText(i18n("Back to Library"));
    backToLibrary->setIcon(QIcon::fromTheme(QStringLiteral("go-previous"), QIcon::fromTheme(QStringLiteral("arrow-left"))));
    actionCollection()->setDefaultShortcuts(backToLibrary, KStandardShortcut::back());
    connect(backToLibrary, &QAction::triggered, this, [this] {
        // Leaving the document for the Library also leaves fullscreen:
        // the Library is a windowed browsing surface.
        if (m_fullScreenAction && m_fullScreenAction->isChecked()) {
            m_fullScreenAction->trigger();
        }
        setActiveTab(0);
    });

    m_nextTabAction = actionCollection()->addAction(QStringLiteral("tab-next"));
    m_nextTabAction->setText(i18n("Next Tab"));
    actionCollection()->setDefaultShortcuts(m_nextTabAction, KStandardShortcut::tabNext());
    m_nextTabAction->setEnabled(false);
    connect(m_nextTabAction, &QAction::triggered, this, &Shell::activateNextTab);

    m_prevTabAction = actionCollection()->addAction(QStringLiteral("tab-previous"));
    m_prevTabAction->setText(i18n("Previous Tab"));
    actionCollection()->setDefaultShortcuts(m_prevTabAction, KStandardShortcut::tabPrev());
    m_prevTabAction->setEnabled(false);
    connect(m_prevTabAction, &QAction::triggered, this, &Shell::activatePrevTab);

    // add shortcuts for Shift+Alt+1 to Shift+Alt+9 to switch tabs(browser logic- Shift+Alt+9 is always going to be last tab)
    for (int i = 1; i <= 9; ++i) {
        QAction *action = actionCollection()->addAction(QStringLiteral("tab-switch-%1").arg(i));
        action->setText(i18n("Switch to Tab %1", i));

        // static cast to Qt::Key satisfies Qt 6 strict type checking for QKeySequence
        actionCollection()->setDefaultShortcut(action, QKeySequence(Qt::SHIFT | Qt::ALT | static_cast<Qt::Key>(Qt::Key_0 + i)));
        connect(action, &QAction::triggered, this, [this, i]() {
            const int count = m_tabWidget->count();
            // Tab 0 is the Library; 1..8 address document tabs, 9 the last tab.
            int index = (i == 9) ? count - 1 : i;
            if (index > 0 && index < count && m_tabWidget->isTabVisible(index)) {
                setActiveTab(index);
            }
        });
    }

    m_undoCloseTab = actionCollection()->addAction(QStringLiteral("undo-close-tab"));
    m_undoCloseTab->setText(i18n("Undo close tab"));
    actionCollection()->setDefaultShortcut(m_undoCloseTab, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    m_undoCloseTab->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    m_undoCloseTab->setEnabled(false);
    connect(m_undoCloseTab, &QAction::triggered, this, &Shell::undoCloseTab);

    m_lockSidebarAction = actionCollection()->addAction(QStringLiteral("focus_lock_sidebar"));
    m_lockSidebarAction->setCheckable(true);
    m_lockSidebarAction->setIcon(QIcon::fromTheme(QStringLiteral("lock")));
    m_lockSidebarAction->setText(i18n("Lock Sidebar"));
    connect(m_lockSidebarAction, &QAction::triggered, m_sidebar, &Sidebar::setLocked);
    m_sidebar->addAction(m_lockSidebarAction);
}

void Shell::saveProperties(KConfigGroup &group)
{
    if (!m_isValid) { // part couldn't be loaded, nothing to save
        return;
    }

    // Gather lists of settings to preserve
    QStringList urls;
    for (const TabState &tab : std::as_const(m_tabs)) {
        urls.append(tab.part->url().url());
    }
    group.writePathEntry(SESSION_URL_KEY, urls);
    group.writeEntry(SESSION_TAB_KEY, m_tabWidget->currentIndex());
}

void Shell::readProperties(const KConfigGroup &group)
{
    // Reopen documents based on saved settings
    QStringList urls = group.readPathEntry(SESSION_URL_KEY, QStringList());
    int desiredTab = group.readEntry<int>(SESSION_TAB_KEY, 0);

    while (!urls.isEmpty()) {
        openUrl(QUrl(urls.takeFirst()));
    }

    if (desiredTab < 0 || desiredTab >= m_tabWidget->count()) {
        desiredTab = 0;
    }
    setActiveTab(desiredTab);
}

void Shell::fileOpen()
{
    // this slot is called whenever the File->Open menu is selected,
    // the Open shortcut is pressed (usually CTRL+O) or the Open toolbar
    // button is clicked
    const int currentTab = m_tabWidget->currentIndex();
    const int docIdx = currentTab <= 0 ? 0 : currentTab - 1;
    if (!m_fileformatsscanned) {
        const KDocumentViewer *const viewer = qobject_cast<KDocumentViewer *>(m_tabs[docIdx].part);
        Q_ASSERT(viewer);

        m_fileformats = viewer->supportedMimeTypes();

        m_fileformatsscanned = true;
    }

    QUrl startDir;
    const KParts::ReadWritePart *const curPart = m_tabs[docIdx].part;
    if (curPart->url().isLocalFile()) {
        startDir = KIO::upUrl(curPart->url());
    }
    if (startDir.isEmpty() || (startDir == QUrl::fromLocalFile(QDir::rootPath()))) {
        startDir = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }

    QPointer<QFileDialog> dlg(new QFileDialog(this));
    dlg->setDirectoryUrl(startDir);
    dlg->setAcceptMode(QFileDialog::AcceptOpen);
    dlg->setOption(QFileDialog::HideNameFilterDetails, true);
    dlg->setFileMode(QFileDialog::ExistingFiles); // Allow selection of more than one file

    QMimeDatabase mimeDatabase;
    // Unfortunately non Plasma file dialogs don't support the "All supported files" when using
    // setMimeTypeFilters instead of setNameFilters, so for those use setNameFilters which is a bit
    // worse because doesn't show you pdf files named bla.blo when you say "show me the pdf files", but
    // that's solvable by choosing "All Files" and it's not that common while it's more convenient to
    // only get shown the files that the application can open by default instead of all of them
    const bool useMimeTypeFilters = qgetenv("XDG_CURRENT_DESKTOP").toLower() == "kde";
    if (useMimeTypeFilters) {
        QStringList mimetypes;
        for (const QString &mimeName : std::as_const(m_fileformats)) {
            QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeName);
            mimetypes << mimeType.name();
        }
        mimetypes.prepend(QStringLiteral("application/octet-stream"));
        dlg->setMimeTypeFilters(mimetypes);
    } else {
        QSet<QString> globPatterns;
        QMap<QString, QStringList> namedGlobs;
        for (const QString &mimeName : std::as_const(m_fileformats)) {
            QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeName);
            const QStringList globs(mimeType.globPatterns());
            if (globs.isEmpty()) {
                continue;
            }

            globPatterns.unite(QSet<QString>(globs.begin(), globs.end()));

            namedGlobs[mimeType.comment()].append(globs);
        }
        QStringList namePatterns;
        for (auto it = namedGlobs.cbegin(); it != namedGlobs.cend(); ++it) {
            namePatterns.append(it.key() + QLatin1String(" (") + it.value().join(QLatin1Char(' ')) + QLatin1Char(')'));
        }

        const QStringList allGlobPatterns = globPatterns.values();
        namePatterns.prepend(i18n("All files (*)"));
        namePatterns.prepend(i18n("All supported files (%1)", allGlobPatterns.join(QLatin1Char(' '))));
        dlg->setNameFilters(namePatterns);
    }

    dlg->setWindowTitle(i18n("Open Document")); /* cppcheck-suppress nullPointerRedundantCheck ; QPointer things here is not understood*/
    if (dlg->exec() && dlg) {                   /* cppcheck-suppress nullPointerRedundantCheck ; QPointer things here is not understood*/
        const QList<QUrl> urlList = dlg->selectedUrls();
        for (const QUrl &url : urlList) {
            openUrl(url);
        }
    }

    if (dlg) {
        delete dlg.data();
    }
}

void Shell::tryRaise(const QString &startupId)
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_OSX) && !defined(Q_OS_HAIKU)
    if (KWindowSystem::isPlatformWayland()) {
        KWindowSystem::setCurrentXdgActivationToken(startupId);
    } else if (KWindowSystem::isPlatformX11()) {
        KStartupInfo::setNewStartupId(window()->windowHandle(), startupId.toUtf8());
    }
#else
    Q_UNUSED(startupId);
#endif

    KWindowSystem::activateWindow(window()->windowHandle());
}

// only called when starting the program
void Shell::setFullScreen(bool useFullScreen)
{
    if (useFullScreen) {
        setWindowState(windowState() | Qt::WindowFullScreen); // set
    } else {
        setWindowState(windowState() & ~Qt::WindowFullScreen); // reset
    }
}

void Shell::setCaption(const QString &caption)
{
    bool modified = false;

    const int activeTab = m_tabWidget->currentIndex();
    if (activeTab > 0) {
        KParts::ReadWritePart *const activePart = m_tabs[activeTab - 1].part;
        QString tabCaption = activePart->url().fileName();
        if (activePart->isModified()) {
            modified = true;
            if (!tabCaption.isEmpty()) {
                tabCaption.append(QStringLiteral(" *"));
            }
        }

        m_tabWidget->setTabText(activeTab, tabCaption);
    }

    // Focus: window title stays "Focus"; the document name
    // lives in the toolbar title instead of the title bar. caption here is
    // the real document title from the part (metadata-aware); with no
    // document it is empty and stays empty (no "Untitled" fallback).
    if (m_docTitle) {
        m_docTitle->setToolTip(caption);
        updateDocTitleElide();
    }
    setCaption(i18n("Focus"), modified);
}

// QLabel cannot elide by itself: keep the full caption in the tooltip and
// show a font-metrics-elided copy. Called on caption changes and label
// resizes so the title shrinks with "..." instead of pushing buttons away.
void Shell::updateDocTitleElide()
{
    if (!m_docTitle) {
        return;
    }
    const QString full = m_docTitle->toolTip();
    const int avail = m_docTitle->contentsRect().width();
    if (avail <= 0) {
        m_docTitle->setText(full);
        return;
    }
    m_docTitle->setText(QFontMetrics(m_docTitle->font()).elidedText(full, Qt::ElideRight, avail));
}

void Shell::showEvent(QShowEvent *e)
{
    if (!menuBar()->isNativeMenuBar() && m_showMenuBarAction) {
        m_showMenuBarAction->setChecked(menuBar()->isVisible());
    }

    KParts::MainWindow::showEvent(e);
}

void Shell::slotUpdateFullScreen()
{
    if (m_fullScreenAction->isChecked()) {
        m_menuBarWasShown = !menuBar()->isHidden();
        menuBar()->hide();

        m_toolBarWasShown = !toolBar()->isHidden();
        // Focus change vs stock Okular: the document toolbar stays visible
        // in fullscreen on document tabs (the back button gives a way out,
        // and page controls stay reachable). Hidden only on the Library
        // tab, which has its own header.
        if (m_tabWidget->currentIndex() > 0) {
            toolBar()->show();
            positionOverlayToolbar();
            styleOverlayButtons();
        } else {
            toolBar()->hide();
        }

        KToggleFullScreenAction::setFullScreen(this, true);
    } else {
        if (m_menuBarWasShown) {
            menuBar()->show();
        }
        // Don't restore the document toolbar onto the Library tab; it is
        // re-shown by setActiveTab() when a document tab becomes active.
        if (m_toolBarWasShown && m_tabWidget->currentIndex() > 0) {
            toolBar()->show();
        }
        KToggleFullScreenAction::setFullScreen(this, false);
    }
}

void Shell::slotShowMenubar()
{
    if (menuBar()->isHidden()) {
        menuBar()->show();
    } else {
        menuBar()->hide();
    }
}

QSize Shell::sizeHint() const
{
    const QSize baseSize = QApplication::primaryScreen()->availableSize() * 0.6;
    // Set an arbitrary yet sensible sane minimum size for very small screens;
    // for example we don't want people using 1366x768 screens to get a tiny
    // default window size of 820 x 460 which will elide most of the toolbar buttons.
    return baseSize.expandedTo(QSize(1000, 700));
}

bool Shell::queryClose()
{
    if (m_tabs.count() > 1) {
        const QString dontAskAgainName = QStringLiteral("ShowTabWarning");
        KMessageBox::ButtonCode dummy = KMessageBox::PrimaryAction;
        if (KMessageBox::shouldBeShownTwoActions(dontAskAgainName, dummy)) {
            QDialog *dialog = new QDialog(this);
            dialog->setWindowTitle(i18n("Confirm Close"));

            QDialogButtonBox *buttonBox = new QDialogButtonBox(dialog);
            buttonBox->setStandardButtons(QDialogButtonBox::Yes | QDialogButtonBox::No);
            KGuiItem::assign(buttonBox->button(QDialogButtonBox::Yes), KGuiItem(i18n("Close Tabs"), QStringLiteral("tab-close")));
            KGuiItem::assign(buttonBox->button(QDialogButtonBox::No), KStandardGuiItem::cancel());

            bool checkboxResult = true;
            const int result = KMessageBox::createKMessageBox(dialog,
                                                              buttonBox,
                                                              QMessageBox::Question,
                                                              i18n("You are about to close %1 tabs. Are you sure you want to continue?", m_tabs.count()),
                                                              QStringList(),
                                                              i18n("Warn me when I attempt to close multiple tabs"),
                                                              &checkboxResult,
                                                              KMessageBox::Notify);

            if (!checkboxResult) {
                KMessageBox::saveDontShowAgainTwoActions(dontAskAgainName, dummy);
            }

            if (result != QDialogButtonBox::Yes) {
                return false;
            }
        }
    }

    for (int i = 0; i < m_tabs.size(); ++i) {
        KParts::ReadWritePart *const part = m_tabs[i].part;

        // To resolve confusion about multiple modified docs, switch to relevant tab
        if (part->isModified()) {
            setActiveTab(i);
        }

        if (!part->queryClose()) {
            return false;
        }
    }
    return true;
}

void Shell::setActiveTab(int tab)
{
    if (tab < 0 || tab >= m_tabWidget->count()) {
        return;
    }

    m_tabWidget->setCurrentIndex(tab);

    // Next/Previous cycle through all tabs, including the Library.
    const bool multi = m_tabWidget->count() > 1;
    m_nextTabAction->setEnabled(multi);
    m_prevTabAction->setEnabled(multi);

    if (tab == 0) {
        // Pinned Library tab: no document part is shown here.
        if (!m_tabs.isEmpty()) {
            const int lastDoc = qBound(1, m_lastDocTab, m_tabWidget->count() - 1);
            createGUI(m_tabs[lastDoc - 1].part);
        }
        KParts::MainWindow::setCaption(i18n("reader"), false);
        // The Library has its own header (+ Open button): hide the document
        // toolbar to free the space, and restore it on document tabs.
        if (!toolBar()->isHidden()) {
            toolBar()->hide();
            m_toolBarHiddenForLibrary = true;
        }
        // Recents only change while a document tab is active, so reload the
        // Library here — otherwise it keeps the model from startup forever.
        refreshRecentsOnWelcomeScreen();
        // Hide the sidebar without disturbing the user's preference.
        m_sidebar->blockSignals(true);
        m_sidebar->hide();
        m_sidebar->blockSignals(false);
        m_printAction->setEnabled(false);
        m_closeAction->setEnabled(false);
        return;
    }

    m_lastDocTab = tab;
    const int doc = tab - 1;

    // Toolbar title follows the part via setCaption() (real document
    // title); nothing to set manually here.

    if (m_toolBarHiddenForLibrary) {
        toolBar()->show();
        m_toolBarHiddenForLibrary = false;
        positionOverlayToolbar();
    }

    if (m_showSidebarAction) {
        m_showSidebarAction->disconnect(m_sidebar);
    }

    // NOTE : createGUI(...) breaks the visibility of the sidebar, so we need
    // to save and restore it
    const bool isSidebarVisible = m_sidebar->isVisible() || (m_showSidebarAction && m_showSidebarAction->isChecked());
    createGUI(m_tabs[doc].part);
    m_sidebar->setVisible(isSidebarVisible);

    // dock KPart's sidebar if new and make it current
    Okular::ViewerInterface *iPart = qobject_cast<Okular::ViewerInterface *>(m_tabs[doc].part);
    Q_ASSERT(iPart);
    QWidget *sideContainer = iPart->getSideContainer();
    if (m_sidebar->indexOf(sideContainer) == -1) {
        m_sidebar->addWidget(sideContainer);
        if (m_sidebar->maximumWidth() > sideContainer->maximumWidth()) {
            m_sidebar->setMaximumWidth(sideContainer->maximumWidth());
        }
    }
    m_sidebar->setCurrentWidget(sideContainer);

    m_showSidebarAction = m_tabs[doc].part->actionCollection()->action(QStringLiteral("show_leftpanel"));
    Q_ASSERT(m_showSidebarAction);
    m_showSidebarAction->disconnect(m_sidebar);
    m_showSidebarAction->setChecked(m_sidebar->isVisibleTo(this));
    connect(m_showSidebarAction, &QAction::triggered, m_sidebar, &Sidebar::setVisible);

    m_printAction->setEnabled(m_tabs[doc].printEnabled);
    m_closeAction->setEnabled(m_tabs[doc].closeEnabled);
    // createGUI() above rebuilds toolbar buttons: re-apply overlay
    // geometry + order/tooltips after the merge completes.
    if (!toolBar()->isHidden()) {
        positionOverlayToolbar();
        styleOverlayButtons();
        // Merges can finish asynchronously after this returns: re-run once
        // on the next event loop pass so late-arriving buttons get styled.
        QTimer::singleShot(0, this, [this]() {
            if (m_tabWidget && toolBar() && !toolBar()->isHidden() && m_tabWidget->currentIndex() > 0) {
                styleOverlayButtons();
            }
        });
    }
}

void Shell::closeTab(int tab)
{
    // The pinned Library tab (index 0) cannot be closed.
    if (tab <= 0 || tab >= m_tabWidget->count()) {
        return;
    }
    const int doc = tab - 1;
    KParts::ReadWritePart *const part = m_tabs[doc].part;
    QUrl url = part->url();
    bool closeSuccess = part->closeUrl();
    if (closeSuccess && m_tabs.count() > 1) {
        if (part->factory()) {
            part->factory()->removeClient(part);
        }
        part->disconnect(this);

        Okular::ViewerInterface *iPart = qobject_cast<Okular::ViewerInterface *>(m_tabs[doc].part);
        Q_ASSERT(iPart);
        QWidget *sideContainer = iPart->getSideContainer();
        m_sidebar->removeWidget(sideContainer);
        connect(part, &QObject::destroyed, sideContainer, &QObject::deleteLater);

        part->deleteLater();
        m_tabs.removeAt(doc);
        m_tabWidget->removeTab(tab);
        m_undoCloseTab->setEnabled(true);
        m_closedTabUrls.append(url);
    } else if (closeSuccess && m_tabs.count() == 1) {
        // Last document closed: clear its title, hide its tab, and return
        // to the Library tab.
        m_tabWidget->setTabText(tab, QString());
        m_tabWidget->setTabToolTip(tab, QString());
        m_tabWidget->setTabVisible(tab, false);
        setActiveTab(0);
    }
}

void Shell::openNewTab(const QUrl &url, const QString &serializedOptions)
{
    const int previousActiveTab = m_tabWidget->currentIndex();
    const int previousDoc = previousActiveTab <= 0 ? 0 : previousActiveTab - 1;
    KParts::ReadWritePart *const activePart = m_tabs[previousDoc].part;

    hideWelcomeScreen();

    bool activateTabIfAlreadyOpen;
    QMetaObject::invokeMethod(activePart, "activateTabIfAlreadyOpenFile", Q_RETURN_ARG(bool, activateTabIfAlreadyOpen));

    if (activateTabIfAlreadyOpen) {
        const int tabIndex = findTabIndex(url);

        if (tabIndex >= 0) {
            setActiveTab(tabIndex + 1);
            m_recent->addUrl(url);
            m_welcomeScreen->noteDocumentOpened(url);
            return;
        }
    }

    const int newIndex = m_tabs.size();

    // Make new part
    m_tabs.append(TabState(m_partFactory->create<KParts::ReadWritePart>(this)));
    connectPart(m_tabs[newIndex].part);

    // Update GUI
    KParts::ReadWritePart *const part = m_tabs[newIndex].part;
    m_tabWidget->addTab(part->widget(), url.fileName());
    m_tabWidget->setTabToolTip(newIndex + 1, url.fileName());

    applyOptionsToPart(part, serializedOptions);

    setActiveTab(m_tabs.size());

    if (part->openUrl(url)) {
        m_recent->addUrl(url);
        m_welcomeScreen->noteDocumentOpened(url);
    } else {
        setActiveTab(previousActiveTab);
        closeTab(m_tabs.size());
        m_recent->removeUrl(url);
    }
    pinLibraryTab();
}

void Shell::applyOptionsToPart(QObject *part, const QString &serializedOptions)
{
    KDocumentViewer *const doc = qobject_cast<KDocumentViewer *>(part);
    const QString find = ShellUtils::find(serializedOptions);
    if (ShellUtils::startInPresentation(serializedOptions)) {
        doc->startPresentation();
    }
    if (ShellUtils::showPrintDialog(serializedOptions)) {
        QMetaObject::invokeMethod(part, "enableStartWithPrint");
    }
    if (ShellUtils::showPrintDialogAndExit(serializedOptions)) {
        QMetaObject::invokeMethod(part, "enableExitAfterPrint");
    }
    if (!find.isEmpty()) {
        QMetaObject::invokeMethod(part, "enableStartWithFind", Q_ARG(QString, find));
    }
}

void Shell::connectPart(const KParts::ReadWritePart *part)
{
    // NOLINTBEGIN(clazy-old-style-connect);
    // We're abusing the fact we know the part is our part here
    connect(this, SIGNAL(moveSplitter(int)), part, SLOT(moveSplitter(int)));
    connect(part, SIGNAL(enablePrintAction(bool)), this, SLOT(setPrintEnabled(bool)));
    connect(part, SIGNAL(enableCloseAction(bool)), this, SLOT(setCloseEnabled(bool)));
    connect(part, SIGNAL(mimeTypeChanged(QMimeType)), this, SLOT(setTabIcon(QMimeType)));
    connect(part, SIGNAL(urlsDropped(QList<QUrl>)), this, SLOT(handleDroppedUrls(QList<QUrl>)));
    connect(part, SIGNAL(maxRecentItemsChanged(int)), this, SLOT(triggerUpdateRecentItems(int)));

    // clang-format off
    // Formatting disabled to keep signature normalized
    connect(part, SIGNAL(requestOpenNewlySignedFile(QString,int)), this, SLOT(openNewlySignedFile(QString,int)));
    connect(part, SIGNAL(fitWindowToPage(QSize,QSize)), this, SLOT(slotFitWindowToPage(QSize,QSize)));
    // clang-format on
    // NOLINTEND(clazy-old-style-connect);
}

void Shell::print()
{
    // Printing needs an open document; the Library tab has none.
    if (m_tabWidget->currentIndex() <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_tabs[m_tabWidget->currentIndex() - 1].part, "slotPrint");
}

void Shell::setPrintEnabled(bool enabled)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabs[i].printEnabled = enabled;
        if (i + 1 == m_tabWidget->currentIndex()) {
            m_printAction->setEnabled(enabled);
        }
    }
}

void Shell::setCloseEnabled(bool enabled)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabs[i].closeEnabled = enabled;
        if (i + 1 == m_tabWidget->currentIndex()) {
            m_closeAction->setEnabled(enabled);
        }
    }
}

void Shell::activateNextTab()
{
    const int count = m_tabWidget->count();
    if (count < 2) {
        return;
    }

    // Cycle through visible tabs (the empty document tab stays hidden).
    int next = m_tabWidget->currentIndex();
    for (int step = 0; step < count; ++step) {
        next = (next + 1) % count;
        if (m_tabWidget->isTabVisible(next)) {
            setActiveTab(next);
            return;
        }
    }
}

void Shell::activatePrevTab()
{
    const int count = m_tabWidget->count();
    if (count < 2) {
        return;
    }

    int prev = m_tabWidget->currentIndex();
    for (int step = 0; step < count; ++step) {
        prev = (prev + count - 1) % count;
        if (m_tabWidget->isTabVisible(prev)) {
            setActiveTab(prev);
            return;
        }
    }
}

void Shell::undoCloseTab()
{
    if (m_closedTabUrls.isEmpty()) {
        return;
    }

    const QUrl lastTabUrl = m_closedTabUrls.takeLast();

    if (m_closedTabUrls.isEmpty()) {
        m_undoCloseTab->setEnabled(false);
    }

    openUrl(lastTabUrl);
}

void Shell::setTabIcon(const QMimeType &mimeType)
{
    int i = findTabIndex(sender());
    if (i != -1) {
        m_tabWidget->setTabIcon(i + 1, QIcon::fromTheme(mimeType.iconName()));
    }
}

int Shell::findTabIndex(QObject *sender) const
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].part == sender) {
            return i;
        }
    }
    return -1;
}

int Shell::findTabIndex(const QUrl &url) const
{
    auto it = std::find_if(m_tabs.begin(), m_tabs.end(), [&url](const TabState state) { return state.part->url() == url; });
    return (it != m_tabs.end()) ? std::distance(m_tabs.begin(), it) : -1;
}

void Shell::handleDroppedUrls(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        openUrl(url);
    }
}

void Shell::moveTabData(int from, int to)
{
    if (from == 0 || to == 0) {
        // The Library tab stays pinned at index 0. Moving it back preserves
        // the relative order of the document tabs, so m_tabs is untouched.
        m_tabWidget->blockSignals(true);
        const int libAt = m_tabWidget->indexOf(m_welcomeScreen);
        if (libAt > 0) {
            m_tabWidget->tabBar()->moveTab(libAt, 0);
        }
        m_tabWidget->blockSignals(false);
        pinLibraryTab();
        return;
    }
    m_tabs.move(from - 1, to - 1);
}

void Shell::slotFitWindowToPage(const QSize pageViewSize, const QSize pageSize)
{
    const int xOffset = pageViewSize.width() - pageSize.width();
    const int yOffset = pageViewSize.height() - pageSize.height();
    showNormal();
    resize(width() - xOffset, height() - yOffset);
    Q_EMIT moveSplitter(pageSize.width());
}

void Shell::hideWelcomeScreen()
{
    m_centralStackedWidget->setCurrentWidget(m_tabWidget);
    m_sidebar->setVisible(m_showSidebarAction->isChecked());
    m_showSidebarAction->setEnabled(true);
}

void Shell::showWelcomeScreen()
{
    // Welcome screen removed in this fork: always show the tab widget instead.
    // (Recent documents remain available via File > Open Recent.)
    hideWelcomeScreen();
}

void Shell::refreshRecentsOnWelcomeScreen()
{
    saveRecents();
    m_welcomeScreen->loadRecents();
}

void Shell::forgetRecentItem(QUrl const &url)
{
    if (m_recent != nullptr) {
        m_recent->removeUrl(url);
        saveRecents();
        m_welcomeScreen->forgetLibraryItem(url);
        refreshRecentsOnWelcomeScreen();
    }
}

void Shell::triggerUpdateRecentItems(const int maxItems)
{
    m_recent->setMaxItems(maxItems);
    m_welcomeScreen->setMaxRecentItems(m_recent->maxItems());
    // saveRecents() dumps the recent files in correct order to KConfigGroup, respecting the allowed no. of recent items, discarding older items if needed
    refreshRecentsOnWelcomeScreen();
    m_recent->loadEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

void Shell::readRecentFilesSettings()
{
    // Read no. of max. recent items from focuspartrc, populate File->Open Recent menu-item as well as recentsListView on welcome screen
    QString configFilePath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QLatin1Char('/') + QLatin1String("focuspartrc");
    const KConfigGroup confgrp = KSharedConfig::openConfig(configFilePath).data()->group(QStringLiteral("General"));
    const int defaultMaxRecentItems = 10;
    int maxRecentItems = confgrp.readEntry<int>("MaxRecentItems", defaultMaxRecentItems);
    m_recent->setMaxItems(maxRecentItems);
    m_welcomeScreen->setMaxRecentItems(m_recent->maxItems());
    m_welcomeScreen->loadRecents();
    m_recent->loadEntries(KSharedConfig::openConfig()->group(RecentFilesGroupKey()));
}

#include "moc_shell.cpp"
#include "shell.moc"
