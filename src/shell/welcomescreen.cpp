/*
    SPDX-FileCopyrightText: 2021 Jiří Wolker <woljiri@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "welcomescreen.h"

#include <KConfigGroup>
#include <KIO/OpenFileManagerWindowJob>
#include <KSharedConfig>

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QToolButton>

#include "gui/recentitemsmodel.h"

// Library filter: search text + format category chips + sorting.
class LibraryFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    enum SortMode { SortRecent, SortName, SortSize };

    explicit LibraryFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    void setSearchText(const QString &text)
    {
        if (m_searchText == text) {
            return;
        }
        m_searchText = text.trimmed();
        invalidateFilter();
    }

    void setCategory(const QString &category)
    {
        if (m_category == category) {
            return;
        }
        m_category = category;
        invalidateFilter();
    }

    void setSortMode(SortMode mode)
    {
        if (m_sortMode == mode) {
            return;
        }
        m_sortMode = mode;
        invalidate();
        // Newest first for recency, biggest first for size, A-Z for names.
        sort(0, (mode == SortName) ? Qt::AscendingOrder : Qt::DescendingOrder);
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
        if (!idx.isValid()) {
            return false;
        }
        if (!m_searchText.isEmpty()) {
            const QString title = idx.data(Qt::DisplayRole).toString();
            const QString url = idx.data(RecentItemsModel::UrlRole).toString();
            if (!title.contains(m_searchText, Qt::CaseInsensitive) && !url.contains(m_searchText, Qt::CaseInsensitive)) {
                return false;
            }
        }
        if (m_category.isEmpty() || m_category == QLatin1String("all")) {
            return true;
        }
        const QString fmt = idx.data(RecentItemsModel::FormatRole).toString();
        if (m_category == QLatin1String("pdf")) {
            return fmt == QLatin1String("PDF");
        }
        if (m_category == QLatin1String("epub")) {
            return fmt == QLatin1String("EPUB");
        }
        if (m_category == QLatin1String("md")) {
            return fmt == QLatin1String("MD") || fmt == QLatin1String("MARKDOWN");
        }
        if (m_category == QLatin1String("other")) {
            return fmt != QLatin1String("PDF") && fmt != QLatin1String("EPUB") && fmt != QLatin1String("MD") && fmt != QLatin1String("MARKDOWN");
        }
        return true;
    }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        switch (m_sortMode) {
        case SortName:
            return left.data(Qt::DisplayRole).toString().compare(right.data(Qt::DisplayRole).toString(), Qt::CaseInsensitive) < 0;
        case SortSize:
            return left.data(RecentItemsModel::BytesRole).toLongLong() < right.data(RecentItemsModel::BytesRole).toLongLong();
        case SortRecent:
        default:
            // Model rows run newest-first, so "less" is the higher row.
            // (Applied with DescendingOrder, this still leads with row 0.)
            return left.row() > right.row();
        }
    }

private:
    QString m_searchText;
    QString m_category = QStringLiteral("all");
    SortMode m_sortMode = SortRecent;
};

static void resolveSourceItem(const QAbstractItemModel *model, const QModelIndex &index, const RecentItemsModel **srcModelOut, QModelIndex *srcIndexOut)
{
    if (const auto *proxy = qobject_cast<const QSortFilterProxyModel *>(model)) {
        *srcIndexOut = proxy->mapToSource(index);
        *srcModelOut = qobject_cast<const RecentItemsModel *>(proxy->sourceModel());
        return;
    }
    // index.model() may be the proxy even if `model` passed to editorEvent is the source.
    if (const auto *proxy = qobject_cast<const QSortFilterProxyModel *>(index.model())) {
        *srcIndexOut = proxy->mapToSource(index);
        *srcModelOut = qobject_cast<const RecentItemsModel *>(proxy->sourceModel());
        return;
    }
    *srcIndexOut = index;
    *srcModelOut = qobject_cast<const RecentItemsModel *>(model ? model : index.model());
}

static QPair<QColor, QColor> badgeColors(const QString &format, const QPalette &fallback)
{
    // Opaque badges: PDF red, EPUB indigo, MD green. White text keeps them
    // readable on the dark library theme.
    if (format == QLatin1String("PDF")) {
        return {QColor(0xB3, 0x26, 0x1E), QColor(0xFF, 0xFF, 0xFF)};
    }
    if (format == QLatin1String("EPUB")) {
        return {QColor(0x39, 0x49, 0xAB), QColor(0xFF, 0xFF, 0xFF)};
    }
    if (format == QLatin1String("MD") || format == QLatin1String("MARKDOWN")) {
        return {QColor(0x2E, 0x7D, 0x32), QColor(0xFF, 0xFF, 0xFF)};
    }
    if (format == QLatin1String("DJVU")) {
        return {QColor(0x6A, 0x1B, 0x9A), QColor(0xFF, 0xFF, 0xFF)};
    }
    if (format == QLatin1String("COMIC")) {
        return {QColor(0xEF, 0x6C, 0x00), QColor(0xFF, 0xFF, 0xFF)};
    }
    if (format == QLatin1String("MOBI")) {
        return {QColor(0x00, 0x83, 0x8F), QColor(0xFF, 0xFF, 0xFF)};
    }
    if (format == QLatin1String("IMG")) {
        return {QColor(0x54, 0x6E, 0x7A), QColor(0xFF, 0xFF, 0xFF)};
    }
    return {fallback.mid().color(), fallback.windowText().color()};
}

// Blit a cover pixmap aspect-filled (center-cropped) into rect with rounded
// corners + frame. Shared by the grid cell and the list row.
static void paintCoverThumb(QPainter *painter, const QRect &rect, const QPixmap &cover, int radius, const QColor &frame)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(rect), radius, radius);
    const QSize px = cover.size();
    const double targetAspect = double(rect.width()) / qMax(1, rect.height());
    const double pxAspect = double(qMax(1, px.width())) / qMax(1, px.height());
    QRect src(QPoint(0, 0), px);
    if (pxAspect > targetAspect) {
        const int w = int(px.height() * targetAspect);
        src.setX((px.width() - w) / 2);
        src.setWidth(w);
    } else if (pxAspect < targetAspect) {
        const int h = int(px.width() / targetAspect);
        src.setY((px.height() - h) / 2);
        src.setHeight(h);
    }
    painter->save();
    painter->setClipPath(path);
    painter->drawPixmap(rect, cover, src);
    painter->restore();
    QPen framePen(frame);
    framePen.setWidth(1);
    painter->setPen(framePen);
    painter->drawPath(path);
}

class RecentsListItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit RecentsListItemDelegate(WelcomeScreen *welcomeScreen)
        : m_welcomeScreen(welcomeScreen)
    {
    }

    // Query the view directly: the style option's widget is null in sizeHint
    // and not always the viewport in paint, so inferring IconMode from it
    // mispaints grid cells as list rows.
    bool gridMode() const
    {
        return m_welcomeScreen && m_welcomeScreen->isGridViewMode();
    }

    WelcomeScreen *welcomeScreen() const
    {
        return m_welcomeScreen;
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (gridMode()) {
            Q_UNUSED(option);
            Q_UNUSED(index);
            return QSize(168, 232);
        }
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(200, 84);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const RecentItemsModel *srcModel = nullptr;
        QModelIndex srcIndex;
        resolveSourceItem(index.model(), index, &srcModel, &srcIndex);
        if (!srcModel || !srcIndex.isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QPalette::ColorGroup group = (opt.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
        Q_UNUSED(group);
        // Library design tokens (same values as applyLibraryTheme): read
        // them as constants instead of opt.palette so item text stays
        // white no matter which palette propagates through the tab widget.
        const QColor border(0x39, 0x34, 0x2D);
        const QColor titleColor(0xF2, 0xF0, 0xEB);
        const QColor subColor(0xB0, 0xAA, 0xA0);

        const QString title = index.data(Qt::DisplayRole).toString();
        const QString format = index.data(RecentItemsModel::FormatRole).toString();
        const QString size = index.data(RecentItemsModel::SizeRole).toString();

        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = (opt.state & QStyle::State_MouseOver) && !selected;

        if (gridMode()) {
            paintCard(painter, opt, title, format, size, index.data(RecentItemsModel::ThumbnailRole).value<QPixmap>(), titleColor, subColor, selected, hovered);
            painter->restore();
            return;
        }

        // Flat table row (file-manager style): cover, single-line name,
        // right-aligned Size + Modified columns. Column metrics mirror the
        // list header (66 / 90 / 16 / 120 / 12).
        constexpr int kRowMargin = 12;
        constexpr int kCoverW = 42;
        constexpr int kCoverH = 56;
        constexpr int kSizeW = 90;
        constexpr int kModW = 120;
        constexpr int kColGap = 16;
        const QRect row = opt.rect;
        if (selected) {
            painter->fillRect(row, QColor(0xF0, 0xAA, 0x3C, 30));
        } else if (hovered) {
            painter->fillRect(row, QColor(0xFF, 0xFF, 0xFF, 14));
        }

        const QRect coverRect(row.left() + kRowMargin, row.top() + qMax(0, (row.height() - kCoverH) / 2), kCoverW, kCoverH);
        const QPixmap listCover = index.data(RecentItemsModel::ThumbnailRole).value<QPixmap>();
        if (!listCover.isNull()) {
            paintCoverThumb(painter, coverRect, listCover, 6, border);
        } else {
            const auto badge = badgeColors(format, opt.palette);
            QPainterPath badgePath;
            badgePath.addRoundedRect(QRectF(coverRect), 6, 6);
            painter->fillPath(badgePath, badge.first);
            QFont badgeFont = opt.font;
            badgeFont.setBold(true);
            badgeFont.setPointSize(qMax(8, badgeFont.pointSize() - 2));
            painter->setFont(badgeFont);
            painter->setPen(badge.second);
            painter->drawText(coverRect, Qt::AlignCenter, QFontMetrics(badgeFont).elidedText(format, Qt::ElideRight, kCoverW - 6));
        }

        const int modRight = row.right() - kRowMargin;
        const QRect modRect(modRight - kModW + 1, row.top(), kModW, row.height());
        const QRect sizeRect(modRect.left() - kColGap - kSizeW, row.top(), kSizeW, row.height());
        const int textLeft = coverRect.right() + kRowMargin;
        const int textWidth = qMax(40, sizeRect.left() - kColGap - textLeft);

        QFont titleFont = opt.font;
        titleFont.setBold(false);
        const QFontMetrics titleFm(titleFont);
        painter->setFont(titleFont);
        painter->setPen(titleColor);
        painter->drawText(QRect(textLeft, row.top(), textWidth, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          titleFm.elidedText(title, Qt::ElideRight, textWidth));

        QFont metaFont = opt.font;
        metaFont.setPointSize(qMax(8, metaFont.pointSize() - 1));
        const QFontMetrics metaFm(metaFont);
        painter->setFont(metaFont);
        painter->setPen(subColor);
        painter->drawText(sizeRect, Qt::AlignRight | Qt::AlignVCenter, metaFm.elidedText(size, Qt::ElideRight, kSizeW));
        const QDateTime modified = index.data(RecentItemsModel::ModifiedRole).toDateTime();
        const QString modText = modified.isValid() ? QLocale().toString(modified.date(), QStringLiteral("d MMM yyyy")) : QStringLiteral("—");
        painter->drawText(modRect, Qt::AlignRight | Qt::AlignVCenter, metaFm.elidedText(modText, Qt::ElideRight, kModW));

        Q_UNUSED(style);
        painter->restore();
    }

    // Grid cell inspired by a cover-thumbnail browser: portrait cover on top,
    // two-line centered title, one-line gray subtitle. Real first-page
    // covers come from RecentItemsModel::ThumbnailRole (PDFs, images);
    // anything else keeps a styled placeholder below.
    void paintCard(QPainter *painter, const QStyleOptionViewItem &opt, const QString &title, const QString &format, const QString &size, const QPixmap &cover,
                   const QColor &titleColor, const QColor &subColor, bool selected, bool hovered) const
    {
        const QRect outer = opt.rect.adjusted(8, 6, -8, -6);

        // Selection / hover wash behind the whole cell (the reference has no
        // card chrome, so keep this subtle).
        if (selected || hovered) {
            QPainterPath wash;
            wash.addRoundedRect(QRectF(outer), 10, 10);
            if (selected) {
                painter->fillPath(wash, QColor(0xF0, 0xAA, 0x3C, 32));
                QPen selPen(QColor(0xF0, 0xAA, 0x3C, 150));
                selPen.setWidth(1);
                painter->setPen(selPen);
                painter->drawPath(wash);
            } else {
                painter->fillPath(wash, QColor(0xFF, 0xFF, 0xFF, 14));
            }
        }

        const auto badge = badgeColors(format, opt.palette);
        const bool isBook = format == QLatin1String("EPUB") || format == QLatin1String("MOBI") || format == QLatin1String("COMIC");

        // Portrait cover, centered. Matches the ~3:4 covers in the reference.
        const int coverW = qMin(112, outer.width() - 16);
        const int coverH = 148;
        const QRect coverRect(outer.center().x() - coverW / 2, outer.top() + 4, coverW, coverH);

        // Drop shadow.
        QPainterPath shadow;
        shadow.addRoundedRect(QRectF(coverRect.translated(1, 2)), 4, 4);
        painter->fillPath(shadow, QColor(0, 0, 0, 70));

        QPainterPath coverPath;
        coverPath.addRoundedRect(QRectF(coverRect), 4, 4);
        if (!cover.isNull()) {
            paintCoverThumb(painter, coverRect, cover, 4, QColor(0, 0, 0, 60));
        } else if (isBook) {
            painter->fillPath(coverPath, badge.first);
            // Spine highlight on the left edge.
            painter->save();
            painter->setClipPath(coverPath);
            painter->fillRect(QRect(coverRect.left(), coverRect.top(), 5, coverRect.height()), QColor(0, 0, 0, 45));
            painter->restore();
            // Large format initial, like a book cover title block.
            QFont coverFont = opt.font;
            coverFont.setBold(true);
            coverFont.setPointSize(22);
            painter->setFont(coverFont);
            painter->setPen(badge.second);
            const QString initial = format.isEmpty() ? QStringLiteral("?") : format.left(1);
            painter->drawText(coverRect.adjusted(6, 10, -6, -34), Qt::AlignHCenter | Qt::AlignVCenter, initial);
            QFont fmtFont = opt.font;
            fmtFont.setBold(true);
            fmtFont.setPointSize(qMax(8, fmtFont.pointSize() - 2));
            painter->setFont(fmtFont);
            painter->drawText(QRect(coverRect.left(), coverRect.bottom() - 26, coverRect.width(), 20), Qt::AlignHCenter, QFontMetrics(fmtFont).elidedText(format, Qt::ElideRight, coverW - 12));
        } else {
            // Paper page: off-white with fake text lines to read as a preview.
            painter->fillPath(coverPath, QColor(0xEC, 0xE9, 0xE2));
            painter->save();
            painter->setClipPath(coverPath);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0x9A, 0x95, 0x8A, 160));
            const int lineX = coverRect.left() + 12;
            const int lineW = coverW - 24;
            int lineY = coverRect.top() + 16;
            const int gaps[6] = {9, 9, 9, 14, 9, 9};
            for (int i = 0; i < 6 && lineY + 3 < coverRect.bottom() - 22; ++i) {
                const int w = (i % 3 == 2) ? lineW * 2 / 3 : lineW;
                painter->drawRect(QRect(lineX, lineY, w, 3));
                lineY += gaps[i] + 3;
            }
            // Small format tag at the bottom of the page.
            QFont tagFont = opt.font;
            tagFont.setBold(true);
            tagFont.setPointSize(8);
            painter->setFont(tagFont);
            painter->setPen(badge.first);
            painter->drawText(QRect(coverRect.left(), coverRect.bottom() - 20, coverRect.width(), 16), Qt::AlignHCenter, QFontMetrics(tagFont).elidedText(format, Qt::ElideRight, coverW - 12));
            painter->restore();
            QPen framePen(QColor(0, 0, 0, 60));
            framePen.setWidth(1);
            painter->setPen(framePen);
            painter->drawPath(coverPath);
        }

        // Title: up to 2 centered lines.
        QFont titleFont = opt.font;
        titleFont.setPointSize(qMax(8, titleFont.pointSize() - 1));
        const QFontMetrics titleFm(titleFont);
        painter->setFont(titleFont);
        painter->setPen(titleColor);
        const int textTop = coverRect.bottom() + 8;
        const int lineH = titleFm.height();
        const int textW = outer.width();
        QString line1 = title;
        QString line2;
        if (titleFm.horizontalAdvance(title) > textW) {
            // Greedy word split into two lines, second line elided.
            const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            line1.clear();
            int i = 0;
            for (; i < words.size(); ++i) {
                const QString cand = line1.isEmpty() ? words[i] : line1 + QLatin1Char(' ') + words[i];
                if (titleFm.horizontalAdvance(cand) > textW) {
                    break;
                }
                line1 = cand;
            }
            if (line1.isEmpty() && !words.isEmpty()) {
                line1 = titleFm.elidedText(words.first(), Qt::ElideRight, textW);
                i = 1;
            }
            line2 = titleFm.elidedText(words.mid(i).join(QLatin1Char(' ')), Qt::ElideRight, textW);
        }
        painter->drawText(QRect(outer.left(), textTop, textW, lineH + 2), Qt::AlignHCenter | Qt::AlignTop, line1);
        int subTop = textTop + lineH + 2;
        if (!line2.isEmpty()) {
            painter->drawText(QRect(outer.left(), subTop, textW, lineH + 2), Qt::AlignHCenter | Qt::AlignTop, line2);
            subTop += lineH + 2;
        }

        // Subtitle: format + size (no author metadata in the model).
        QFont subFont = opt.font;
        subFont.setPointSize(qMax(8, subFont.pointSize() - 2));
        const QFontMetrics subFm(subFont);
        painter->setFont(subFont);
        painter->setPen(subColor);
        const QString subtitle = QStringLiteral("%1 · %2").arg(format, size);
        painter->drawText(QRect(outer.left(), subTop + 1, textW, subFm.height() + 2), Qt::AlignHCenter | Qt::AlignTop, subFm.elidedText(subtitle, Qt::ElideRight, textW));
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *aModel, const QStyleOptionViewItem &styleOptionViewItem, const QModelIndex &index) override
    {
        const RecentItemsModel *srcModel = nullptr;
        QModelIndex srcIndex;
        resolveSourceItem(aModel, index, &srcModel, &srcIndex);
        const RecentItemsModel::RecentItem *item = srcModel ? srcModel->getItem(srcIndex) : nullptr;

        QPoint menuPosition;

        if (item != nullptr) {
            bool willOpenMenu = false;
            if (event->type() == QEvent::ContextMenu) {
                willOpenMenu = true;
                menuPosition = static_cast<QContextMenuEvent *>(event)->globalPos();
            }
            if (event->type() == QEvent::MouseButtonPress) {
                if (static_cast<QMouseEvent *>(event)->button() == Qt::MouseButton::RightButton) {
                    willOpenMenu = true;
                    menuPosition = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
                }
            }

            if (willOpenMenu) {
                event->accept();

                QMenu menu;

                QAction *copyPathAction = new QAction(i18n("&Copy Path"));
                copyPathAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
                connect(copyPathAction, &QAction::triggered, this, [item]() {
                    QString path;
                    if (item->url.isLocalFile()) {
                        path = item->url.toLocalFile();
                    } else {
                        path = item->url.toString();
                    }
                    QGuiApplication::clipboard()->setText(path);
                });
                menu.addAction(copyPathAction);

                QAction *showDirectoryAction = new QAction(i18n("&Open Containing Folder"));
                showDirectoryAction->setIcon(QIcon::fromTheme(QStringLiteral("document-open-folder")));
                connect(showDirectoryAction, &QAction::triggered, this, [item]() {
                    if (item->url.isLocalFile()) {
                        KIO::highlightInFileManager({item->url});
                    }
                });
                menu.addAction(showDirectoryAction);
                if (!item->url.isLocalFile()) {
                    showDirectoryAction->setEnabled(false);
                }

                QAction *forgetItemAction = new QAction(i18nc("recent items context menu", "&Forget This Item"));
                forgetItemAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear-history")));
                connect(forgetItemAction, &QAction::triggered, this, [this, item]() { Q_EMIT welcomeScreen()->forgetRecentItem(item->url); });
                menu.addAction(forgetItemAction);

                menu.exec(menuPosition);

                return true;
            }
        }

        return QStyledItemDelegate::editorEvent(event, aModel, styleOptionViewItem, index);
    }

private:
    WelcomeScreen *m_welcomeScreen;
};

// Design tokens for the library screen (WCAG AA checked on the dark theme).
// Spacing scale: 4 / 8 / 12 / 16 / 24. Control height 36, search 40.
// The document viewer itself is untouched: palette + stylesheet only ever
// apply to this widget's subtree.
static void applyLibraryTheme(WelcomeScreen *screen)
{
    const QColor window(0x14, 0x13, 0x11);
    const QColor surface(0x1C, 0x1A, 0x17);
    const QColor elevated(0x24, 0x21, 0x1D);
    const QColor ink(0xF2, 0xF0, 0xEB);
    const QColor secondary(0xB0, 0xAA, 0xA0);
    const QColor muted(0x89, 0x82, 0x78);
    const QColor amber(0xF0, 0xAA, 0x3C);
    const QColor amberHover(0xFF, 0xC1, 0x5A);
    const QColor line(0x39, 0x34, 0x2D);

    QPalette pal;
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool disabled = group == QPalette::Disabled;
        pal.setColor(group, QPalette::Window, window);
        pal.setColor(group, QPalette::WindowText, disabled ? muted : ink);
        pal.setColor(group, QPalette::Base, surface);
        pal.setColor(group, QPalette::AlternateBase, surface);
        pal.setColor(group, QPalette::Text, disabled ? muted : ink);
        pal.setColor(group, QPalette::PlaceholderText, muted);
        pal.setColor(group, QPalette::Button, elevated);
        pal.setColor(group, QPalette::ButtonText, disabled ? muted : ink);
        pal.setColor(group, QPalette::Highlight, amber);
        pal.setColor(group, QPalette::HighlightedText, window);
        pal.setColor(group, QPalette::Mid, line);
        pal.setColor(group, QPalette::Dark, QColor(0x0E, 0x0C, 0x0A));
        pal.setColor(group, QPalette::Light, QColor(0x4A, 0x40, 0x36));
        pal.setColor(group, QPalette::Link, amber);
        pal.setColor(group, QPalette::ToolTipBase, elevated);
        pal.setColor(group, QPalette::ToolTipText, ink);
    }
    screen->setAutoFillBackground(true);
    screen->setStyleSheet(QStringLiteral(
        "WelcomeScreen { background-color: #141311; }"
        "WelcomeScreen QLabel { color: #F2F0EB; background: transparent; }"
        "QLineEdit#searchEdit { background-color: #1C1A17; border: 1px solid #39342D; border-radius: 10px; padding: 0 14px; color: #F2F0EB; "
        " selection-background-color: #F0AA3C; selection-color: #141311; }"
        "QLineEdit#searchEdit:focus { border: 1px solid #F0AA3C; }"
        "QLineEdit#searchEdit::clear-button { width: 16px; }"
        "QWidget#headerBar, QWidget#contentWrap { background: transparent; }"
        "QWidget#filterBar { background: transparent; }"
        "QScrollArea#filterScroll { background: transparent; border: none; }"
        "QScrollArea#filterScroll QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }"
        "QScrollArea#filterScroll QScrollBar::handle:horizontal { background: #39342D; border-radius: 4px; min-width: 32px; }"
        "QScrollArea#filterScroll QScrollBar::handle:horizontal:hover { background: #F0AA3C; }"
        "QScrollArea#filterScroll QScrollBar::add-line:horizontal, QScrollArea#filterScroll QScrollBar::sub-line:horizontal { width: 0; }"
        "QWidget#filterBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 16px; padding: 0 16px; min-height: 30px; color: #B0AAA0; font-weight: bold; }"
        "QWidget#filterBar QToolButton:hover { background: #24211D; color: #F2F0EB; border-color: #39342D; }"
        "QWidget#filterBar QToolButton:focus { border-color: #F0AA3C; color: #F2F0EB; }"
        "QWidget#filterBar QToolButton:checked { background: #F0AA3C; border-color: #F0AA3C; color: #141311; }"
        "QWidget#filterBar QToolButton:checked:hover { background: #FFC15A; border-color: #FFC15A; }"
        "QComboBox#sortCombo { background-color: #1C1A17; border: 1px solid #39342D; border-radius: 10px; padding: 0 12px; min-height: 34px; color: #F2F0EB; }"
        "QComboBox#sortCombo:hover { border-color: #898278; }"
        "QComboBox#sortCombo:focus { border: 1px solid #F0AA3C; }"
        "QComboBox#sortCombo QAbstractItemView { background-color: #24211D; color: #F2F0EB; border: 1px solid #39342D; selection-background-color: #F0AA3C; selection-color: #141311; outline: 0; }"
        "QFrame#divider { color: #39342D; }"
        "QPushButton#openDocButton { background-color: #F0AA3C; border: 1px solid #F0AA3C; border-radius: 10px; padding: 0 16px; color: #141311; font-weight: bold; }"
        "QPushButton#openDocButton:hover { background-color: #FFC15A; border-color: #FFC15A; }"
        "QPushButton#openDocButton:focus { border: 1px solid #FFC15A; }"
        "QPushButton#openDocButton:pressed { background-color: #D8942A; }"
        "QToolButton#viewToggle { background: transparent; border: 1px solid transparent; border-radius: 10px; padding: 0 12px; min-height: 34px; color: #B0AAA0; }"
        "QToolButton#viewToggle:hover { background: #24211D; color: #F2F0EB; border-color: #39342D; }"
        "QToolButton#viewToggle:focus { border-color: #F0AA3C; color: #F2F0EB; }"
        "QToolButton#viewToggle:disabled { color: #898278; }"
        "QToolButton#viewToggle:checked { color: #F0AA3C; border-color: rgba(240, 170, 60, 90); background: rgba(240, 170, 60, 26); }"
        "QListView#recentsListView { background: transparent; border: none; outline: 0; }"
        "QListView#recentsListView::item { background: transparent; border: none; }"
        "QListView#recentsListView QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QListView#recentsListView QScrollBar::handle:vertical { background: #39342D; border-radius: 4px; min-height: 32px; }"
        "QListView#recentsListView QScrollBar::handle:vertical:hover { background: #F0AA3C; }"
        "QListView#recentsListView QScrollBar::add-line:vertical, QListView#recentsListView QScrollBar::sub-line:vertical { height: 0; }"
        "QLabel#emptyTitle { color: #F2F0EB; }"
        "QLabel#emptySubtitle { color: #B0AAA0; }"
        "QWidget#listHeader { background: transparent; }"
        "QWidget#listHeader QLabel { color: #B0AAA0; background: transparent; }"));
    Q_UNUSED(amberHover);
    Q_UNUSED(secondary);
    // NOTE: setStyleSheet() resets the widget palette to the style default,
    // so the custom palette must be applied AFTER it (and not before).
    screen->setPalette(pal);
}

WelcomeScreen::WelcomeScreen(QWidget *parent)
    : QWidget(parent)
    , m_recentsModel(new RecentItemsModel)
    , m_proxyModel(new LibraryFilterProxyModel(this))
    , m_filterGroup(new QButtonGroup(this))
    , m_recentsItemDelegate(new RecentsListItemDelegate(this))
{
    Q_ASSERT(parent);

    setupUi(this);

    applyLibraryTheme(this);
    // NOTE: QListView (and its viewport) do not inherit a custom parent
    // palette, so set it explicitly — the delegate paints from it.
    recentsListView->setPalette(palette());
    recentsListView->viewport()->setPalette(palette());
    filterScroll->viewport()->setPalette(palette());

    m_proxyModel->setSourceModel(m_recentsModel);
    // Default order: most recently opened first (model rows already run
    // newest-first; sort explicitly since setSortMode() no-ops when the
    // mode is unchanged).
    static_cast<LibraryFilterProxyModel *>(m_proxyModel)->sort(0, Qt::DescendingOrder);

    connect(openDocButton, &QPushButton::clicked, this, &WelcomeScreen::openClicked);

    recentsListView->setContextMenuPolicy(Qt::DefaultContextMenu);
    recentsListView->setModel(m_proxyModel);
    recentsListView->setItemDelegate(m_recentsItemDelegate);
    recentsListView->setSpacing(0);
    recentsListView->setUniformItemSizes(false);
    recentsListView->setVerticalScrollMode(QListView::ScrollPerPixel);
    connect(recentsListView, &QListView::activated, this, &WelcomeScreen::recentsItemActivated);
    connect(recentsListView, &QListView::clicked, this, &WelcomeScreen::recentsItemActivated);

    connect(m_recentsModel, &RecentItemsModel::layoutChanged, this, &WelcomeScreen::recentListChanged);
    connect(m_recentsModel, &RecentItemsModel::modelReset, this, &WelcomeScreen::recentListChanged);
    connect(m_proxyModel, &QSortFilterProxyModel::layoutChanged, this, &WelcomeScreen::updateEmptyState);
    connect(m_proxyModel, &QSortFilterProxyModel::rowsInserted, this, &WelcomeScreen::updateEmptyState);
    connect(m_proxyModel, &QSortFilterProxyModel::rowsRemoved, this, &WelcomeScreen::updateEmptyState);
    connect(m_proxyModel, &QSortFilterProxyModel::modelReset, this, &WelcomeScreen::updateEmptyState);

    searchEdit->setPlaceholderText(i18nc("on welcome screen", "Search library by title or path…"));
    connect(searchEdit, &QLineEdit::textChanged, this, &WelcomeScreen::onSearchTextChanged);

    m_filterGroup->setExclusive(true);
    m_filterGroup->addButton(filterAll);
    m_filterGroup->addButton(filterPdf);
    m_filterGroup->addButton(filterEpub);
    m_filterGroup->addButton(filterMd);
    m_filterGroup->addButton(filterOther);
    filterAll->setChecked(true);
    connect(filterAll, &QToolButton::clicked, this, &WelcomeScreen::onFilterButtonClicked);
    connect(filterPdf, &QToolButton::clicked, this, &WelcomeScreen::onFilterButtonClicked);
    connect(filterEpub, &QToolButton::clicked, this, &WelcomeScreen::onFilterButtonClicked);
    connect(filterMd, &QToolButton::clicked, this, &WelcomeScreen::onFilterButtonClicked);
    connect(filterOther, &QToolButton::clicked, this, &WelcomeScreen::onFilterButtonClicked);

    connect(sortCombo, &QComboBox::activated, this, &WelcomeScreen::onSortChanged);

    connect(viewToggle, &QToolButton::toggled, this, &WelcomeScreen::onViewModeToggled);

    // Sensible keyboard order through the library controls into the list.
    QWidget::setTabOrder(searchEdit, filterAll);
    QWidget::setTabOrder(filterAll, filterPdf);
    QWidget::setTabOrder(filterPdf, filterEpub);
    QWidget::setTabOrder(filterEpub, filterMd);
    QWidget::setTabOrder(filterMd, filterOther);
    QWidget::setTabOrder(filterOther, sortCombo);
    QWidget::setTabOrder(sortCombo, viewToggle);
    QWidget::setTabOrder(viewToggle, openDocButton);
    QWidget::setTabOrder(openDocButton, recentsListView);

    // Flat-table header for list mode. Column metrics (left 66, Size 90,
    // gap 16, Modified 120, right 12) mirror the delegate's list row.
    m_listHeader = new QWidget(this);
    m_listHeader->setObjectName(QStringLiteral("listHeader"));
    auto *headerLayout = new QHBoxLayout(m_listHeader);
    headerLayout->setContentsMargins(66, 2, 12, 6);
    headerLayout->setSpacing(0);
    m_nameHeader = new QLabel(m_listHeader);
    m_nameHeader->setText(i18nc("library list header", "Name"));
    headerLayout->addWidget(m_nameHeader);
    headerLayout->addStretch(1);
    m_sizeHeader = new QLabel(m_listHeader);
    m_sizeHeader->setText(i18nc("library list header", "Size"));
    m_sizeHeader->setFixedWidth(90);
    m_sizeHeader->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerLayout->addWidget(m_sizeHeader);
    headerLayout->addSpacing(16);
    m_modHeader = new QLabel(m_listHeader);
    m_modHeader->setText(i18nc("library list header", "Modified"));
    m_modHeader->setFixedWidth(120);
    m_modHeader->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerLayout->addWidget(m_modHeader);
    if (QBoxLayout *contentLayout = qobject_cast<QBoxLayout *>(recentsListView->parentWidget()->layout())) {
        contentLayout->insertWidget(contentLayout->indexOf(recentsListView), m_listHeader);
    }
    m_listHeader->setVisible(false); // starts in grid mode
    // Keep header columns aligned with rows when the scrollbar appears.
    connect(recentsListView->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { updateListHeaderMargin(); });

    // Start in grid mode (cover layout); the toggle flips back to list.
    viewToggle->setChecked(true);

    updateEmptyState();
}

WelcomeScreen::~WelcomeScreen()
{
    delete m_recentsModel;
    delete m_recentsItemDelegate;
}

void WelcomeScreen::paintEvent(QPaintEvent *e)
{
    // Guarantee the warm-dark page background no matter what the surrounding
    // tab widget/style paints behind this page.
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x14, 0x13, 0x11));
    QWidget::paintEvent(e);
}

void WelcomeScreen::loadRecents()
{
    m_recentsModel->loadEntries(KSharedConfig::openConfig()->group(QStringLiteral("Recent Files")));
    updateEmptyState();
}

void WelcomeScreen::noteDocumentOpened(const QUrl &url)
{
    m_recentsModel->noteOpened(url);
    updateEmptyState();
}

void WelcomeScreen::forgetLibraryItem(const QUrl &url)
{
    m_recentsModel->removeUrl(url);
    updateEmptyState();
}

void WelcomeScreen::setMaxRecentItems(const int maxItems)
{
    m_recentsModel->setMaxItems(maxItems);
}

bool WelcomeScreen::isGridViewMode() const
{
    return recentsListView->viewMode() == QListView::IconMode;
}

int WelcomeScreen::recentsCount()
{
    return m_recentsModel->rowCount();
}

void WelcomeScreen::recentsItemActivated(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    const QModelIndex srcIndex = m_proxyModel->mapToSource(index);
    const RecentItemsModel::RecentItem *item = m_recentsModel->getItem(srcIndex);
    if (item != nullptr) {
        Q_EMIT recentItemClicked(item->url);
    }
}

void WelcomeScreen::recentListChanged()
{
    updateEmptyState();
}

void WelcomeScreen::onSearchTextChanged(const QString &text)
{
    static_cast<LibraryFilterProxyModel *>(m_proxyModel)->setSearchText(text);
    updateEmptyState();
}

void WelcomeScreen::onFilterButtonClicked()
{
    static_cast<LibraryFilterProxyModel *>(m_proxyModel)->setCategory(currentFilterCategory());
    updateEmptyState();
}

void WelcomeScreen::onSortChanged(int row)
{
    auto *proxy = static_cast<LibraryFilterProxyModel *>(m_proxyModel);
    if (row == 1) {
        proxy->setSortMode(LibraryFilterProxyModel::SortName);
    } else if (row == 2) {
        proxy->setSortMode(LibraryFilterProxyModel::SortSize);
    } else {
        proxy->setSortMode(LibraryFilterProxyModel::SortRecent);
    }
    updateEmptyState();
}

void WelcomeScreen::onViewModeToggled(bool grid)
{
    if (grid) {
        m_listHeader->setVisible(false);
        recentsListView->setViewMode(QListView::IconMode);
        recentsListView->setGridSize(QSize(176, 244));
        recentsListView->setResizeMode(QListView::Adjust);
        recentsListView->setMovement(QListView::Static);
        recentsListView->setSpacing(12);
        recentsListView->setUniformItemSizes(true);
        recentsListView->setWordWrap(true);
        viewToggle->setText(i18nc("on welcome screen", "List"));
        viewToggle->setIcon(QIcon::fromTheme(QStringLiteral("view-list-details")));
    } else {
        recentsListView->setViewMode(QListView::ListMode);
        recentsListView->setGridSize(QSize()); // grid pitch must not leak into list rows
        recentsListView->setWordWrap(false);
        recentsListView->setSpacing(0);
        recentsListView->setUniformItemSizes(false);
        viewToggle->setText(i18nc("on welcome screen", "Grid"));
        viewToggle->setIcon(QIcon::fromTheme(QStringLiteral("view-grid")));
        m_listHeader->setVisible(true);
        updateListHeaderMargin();
    }
    // setViewMode() already triggers a relayout/repaint with the new delegate sizes.
}

void WelcomeScreen::updateListHeaderMargin()
{
    if (!m_listHeader || !m_listHeader->isVisible()) {
        return;
    }
    // The header spans the full view width while rows span the viewport,
    // so add the scrollbar width when it is shown to keep Size/Modified
    // aligned with the row columns.
    QScrollBar *sb = recentsListView->verticalScrollBar();
    const int extra = (sb && sb->isVisibleTo(recentsListView)) ? sb->width() : 0;
    if (auto *hl = qobject_cast<QHBoxLayout *>(m_listHeader->layout())) {
        hl->setContentsMargins(66, 2, 12 + extra, 6);
    }
}

QString WelcomeScreen::currentFilterCategory() const
{
    if (filterPdf->isChecked()) {
        return QStringLiteral("pdf");
    }
    if (filterEpub->isChecked()) {
        return QStringLiteral("epub");
    }
    if (filterMd->isChecked()) {
        return QStringLiteral("md");
    }
    if (filterOther->isChecked()) {
        return QStringLiteral("other");
    }
    return QStringLiteral("all");
}

void WelcomeScreen::updateEmptyState()
{
    const int total = recentsCount();

    const bool hasRows = m_proxyModel->rowCount() > 0;
    recentsListView->setVisible(hasRows);
    emptyState->setVisible(!hasRows);
    if (!hasRows) {
        if (total == 0) {
            emptyTitle->setText(i18nc("on welcome screen", "No recent documents"));
            emptySubtitle->setText(i18nc("on welcome screen", "Open a document to get started — it will show up here."));
        } else {
            emptyTitle->setText(i18nc("on welcome screen", "No matches"));
            emptySubtitle->setText(i18nc("on welcome screen", "Try a different search or filter."));
        }
    }
}

#include "moc_welcomescreen.cpp"
#include "welcomescreen.moc"
