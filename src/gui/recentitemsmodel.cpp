/*
    SPDX-FileCopyrightText: 2021 Jiří Wolker <woljiri@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "recentitemsmodel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QLocale>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

#include <KConfigGroup>
#include <KSharedConfig>

#ifdef HAVE_POPPLER_QT6
#include <poppler-qt6.h>
#endif

RecentItemsModel::RecentItemsModel()
{
}

RecentItemsModel::~RecentItemsModel()
{
}

void RecentItemsModel::loadEntries(const KConfigGroup &cg)
{
    // Full content replacement: notify as a reset so attached views,
    // the filter proxy, and the selection model drop all persistent
    // indexes. Emitting only layoutChanged() here leaves the proxy with
    // a stale source mapping and segfaults the next repaint that touches
    // the selection (QSortFilterProxyModel::parent on a dead index).
    beginResetModel();
    m_recentItems.clear();

    // Primary source: the library's own history (cap kLibraryMaxItems),
    // which outlives the File-menu recents.
    const KConfigGroup libGroup = KSharedConfig::openConfig()->group(QStringLiteral("LibraryFiles"));
    for (int i = 1; i <= kLibraryMaxItems; i++) {
        const QString key = QStringLiteral("File%1").arg(i);
        const QString value = libGroup.readPathEntry(key, QString());
        if (value.isEmpty()) {
            continue;
        }
        const QUrl url = QUrl::fromUserInput(value);
        const QString nameKey = QStringLiteral("Name%1").arg(i);
        const QString nameValue = libGroup.readPathEntry(nameKey, url.fileName());
        m_recentItems.append(RecentItem {nameValue, url, QPixmap()});
    }

    if (m_recentItems.isEmpty()) {
        // First run / upgrade from menu-only history: import the File-menu
        // recents (based on KRecentFilesAction::loadEntries).
        for (int i = 1; i <= maxItems(); i++) {
            const QString key = QStringLiteral("File%1").arg(i);
            const QString value = cg.readPathEntry(key, QString());
            if (value.isEmpty()) {
                continue;
            }
            const QUrl url = QUrl::fromUserInput(value);
            const QString nameKey = QStringLiteral("Name%1").arg(i);
            const QString nameValue = cg.readPathEntry(nameKey, url.fileName());
            m_recentItems.append(RecentItem {nameValue, url, QPixmap()});
        }
    }

    endResetModel();

    scheduleThumbnails();
}

void RecentItemsModel::noteOpened(const QUrl &url)
{
    if (url.isEmpty()) {
        return;
    }
    beginResetModel();
    for (int i = m_recentItems.size() - 1; i >= 0; --i) {
        if (m_recentItems[i].url == url) {
            m_recentItems.removeAt(i);
        }
    }
    QString name = url.fileName();
    if (name.isEmpty()) {
        name = url.toString();
    }
    m_recentItems.prepend(RecentItem {name, url, QPixmap()});
    while (m_recentItems.size() > kLibraryMaxItems) {
        m_recentItems.removeLast();
    }
    saveLibrary();
    endResetModel();

    scheduleThumbnails();
}

void RecentItemsModel::removeUrl(const QUrl &url)
{
    beginResetModel();
    for (int i = m_recentItems.size() - 1; i >= 0; --i) {
        if (m_recentItems[i].url == url) {
            m_recentItems.removeAt(i);
        }
    }
    saveLibrary();
    endResetModel();

    scheduleThumbnails();
}

void RecentItemsModel::saveLibrary() const
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig();
    KConfigGroup group = config->group(QStringLiteral("LibraryFiles"));
    group.deleteGroup();
    KConfigGroup fresh = config->group(QStringLiteral("LibraryFiles"));
    int row = 0;
    for (const RecentItem &item : std::as_const(m_recentItems)) {
        if (row >= kLibraryMaxItems) {
            break;
        }
        ++row;
        fresh.writePathEntry(QStringLiteral("File%1").arg(row), item.url.toString());
        fresh.writePathEntry(QStringLiteral("Name%1").arg(row), item.name);
    }
    config->sync();
}

void RecentItemsModel::clearEntries()
{
    ++m_thumbGeneration;
    m_thumbQueue.clear();
    beginResetModel();
    m_recentItems.clear();
    endResetModel();
}

QHash<int, QByteArray> RecentItemsModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();

    // Custom roles
    roles[IconNameRole] = "iconName";
    roles[UrlRole] = "url";
    roles[FormatRole] = "format";
    roles[SizeRole] = "size";
    roles[BytesRole] = "bytes";
    roles[ThumbnailRole] = "thumbnail";
    roles[ModifiedRole] = "modified";

    return roles;
}

int RecentItemsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    return m_recentItems.size();
}

QVariant RecentItemsModel::data(const QModelIndex &index, int role) const
{
    const RecentItemsModel::RecentItem *item = getItem(index);

    if (item != nullptr) {
        switch (role) {
        case Qt::ItemDataRole::DisplayRole:
            if (item->name.isEmpty()) {
                if (item->url.isLocalFile()) {
                    return item->url.toLocalFile();
                } else {
                    return item->url.toString();
                }
            } else {
                return item->name;
            }

        case Qt::ItemDataRole::ToolTipRole:
            if (item->url.isLocalFile()) {
                return item->url.toLocalFile();
            } else {
                return item->url.toString();
            }

        case Qt::ItemDataRole::DecorationRole:
            if (item->url.isLocalFile()) {
                return m_iconProvider.icon(QFileInfo(item->url.toLocalFile()));
            } else {
                // Fallback icon for remote files.
                return QIcon::fromTheme(QStringLiteral("network-server"));
            }

        case IconNameRole:
            if (item->url.isLocalFile()) {
                return m_iconProvider.icon(QFileInfo(item->url.toLocalFile())).name();
            } else {
                // Fallback icon for remote files.
                return QIcon::fromTheme(QStringLiteral("network-server")).name();
            }

        case UrlRole:
            return item->url.toString();

        case FormatRole:
            return formatForUrl(item->url);

        case SizeRole:
            return sizeForUrl(item->url);

        case BytesRole:
            if (item->url.isLocalFile()) {
                const QFileInfo info(item->url.toLocalFile());
                if (info.exists()) {
                    return info.size();
                }
            }
            return -1;

        case ThumbnailRole:
            if (!item->thumbnail.isNull()) {
                return item->thumbnail;
            }
            return QVariant();

        case ModifiedRole:
            if (item->url.isLocalFile()) {
                const QFileInfo info(item->url.toLocalFile());
                if (info.exists()) {
                    return info.lastModified();
                }
            }
            return QVariant();

        default:
            return QVariant();
        }
    } else {
        return QVariant();
    }
}

RecentItemsModel::RecentItem const *RecentItemsModel::getItem(int index) const
{
    // Storage and model rows share one order: newest first (File1 in the
    // config), so the default view needs no sorting to lead with recents.
    if (m_recentItems.size() > index && index >= 0) {
        return &m_recentItems[index];
    } else {
        return nullptr;
    }
}

RecentItemsModel::RecentItem const *RecentItemsModel::getItem(const QModelIndex &index) const
{
    return getItem(index.row());
}

RecentItemsModel::RecentItem const *RecentItemsModel::mostRecentItem() const
{
    // Entries are stored File1..FileN with File1 the newest.
    if (m_recentItems.isEmpty()) {
        return nullptr;
    }
    return &m_recentItems.first();
}

int RecentItemsModel::maxItems()
{
    return m_maxItems;
}

QString RecentItemsModel::formatForUrl(const QUrl &url)
{
    QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
    const QString suffix = QFileInfo(path).suffix().toUpper();
    if (suffix.isEmpty()) {
        return QStringLiteral("DOC");
    }
    // Normalise common aliases to the badge labels used in the library view.
    if (suffix == QLatin1String("MARKDOWN") || suffix == QLatin1String("TXT")) {
        return QStringLiteral("MD");
    }
    if (suffix == QLatin1String("MOBI") || suffix == QLatin1String("AZW") || suffix == QLatin1String("AZW3")) {
        return QStringLiteral("MOBI");
    }
    if (suffix == QLatin1String("DJVU") || suffix == QLatin1String("DJV")) {
        return QStringLiteral("DJVU");
    }
    if (suffix == QLatin1String("CBZ") || suffix == QLatin1String("CBR") || suffix == QLatin1String("CB7")) {
        return QStringLiteral("COMIC");
    }
    if (suffix == QLatin1String("JPG") || suffix == QLatin1String("JPEG") || suffix == QLatin1String("PNG") || suffix == QLatin1String("TIFF") || suffix == QLatin1String("TIF") || suffix == QLatin1String("WEBP")) {
        return QStringLiteral("IMG");
    }
    return suffix;
}

QString RecentItemsModel::sizeForUrl(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return QStringLiteral("—");
    }
    const QFileInfo info(url.toLocalFile());
    if (!info.exists()) {
        return QStringLiteral("—");
    }
    const qint64 bytes = info.size();
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kb = bytes / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB").arg(QLocale().toString(kb, 'f', 1));
    }
    const double mb = kb / 1024.0;
    if (mb < 1024.0) {
        return QStringLiteral("%1 MB").arg(QLocale().toString(mb, 'f', 1));
    }
    const double gb = mb / 1024.0;
    return QStringLiteral("%1 GB").arg(QLocale().toString(gb, 'f', 2));
}

QString RecentItemsModel::displayNameForItem(const RecentItem &item)
{
    if (!item.name.isEmpty()) {
        return item.name;
    }
    if (item.url.isLocalFile()) {
        return item.url.toLocalFile();
    }
    return item.url.toString();
}

void RecentItemsModel::setMaxItems(const int maxItems)
{
    m_maxItems = maxItems;
}

// Grid covers: exact 2x of the delegate's 112x148 cover, pre-cropped so
// paint just blits.
QSize RecentItemsModel::coverTargetSize()
{
    return QSize(224, 296);
}

bool RecentItemsModel::wantsCover(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return false;
    }
    const QFileInfo info(url.toLocalFile());
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    const QString suffix = info.suffix().toLower();
#ifdef HAVE_POPPLER_QT6
    if (suffix == QLatin1String("pdf")) {
        return true;
    }
#else
    Q_UNUSED(suffix);
#endif
    return suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg") || suffix == QLatin1String("png") || suffix == QLatin1String("tif") ||
        suffix == QLatin1String("tiff") || suffix == QLatin1String("webp") || suffix == QLatin1String("bmp") || suffix == QLatin1String("gif");
}

QString RecentItemsModel::coverCachePath(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return QString();
    }
    const QFileInfo info(url.toLocalFile());
    if (!info.exists()) {
        return QString();
    }
    const QString key = QStringLiteral("%1|%2|%3").arg(url.toString(QUrl::FullyEncoded)).arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1String("/okular-covers/") + hash + QLatin1String(".png");
}

QPixmap RecentItemsModel::normalizeCover(const QPixmap &src)
{
    if (src.isNull()) {
        return QPixmap();
    }
    const QSize target = RecentItemsModel::coverTargetSize();
    const QPixmap scaled = src.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.size() == target) {
        return scaled;
    }
    // Center-crop to the exact target.
    const int x = qMax(0, (scaled.width() - target.width()) / 2);
    const int y = qMax(0, (scaled.height() - target.height()) / 2);
    return scaled.copy(x, y, target.width(), target.height());
}

QPixmap RecentItemsModel::renderCover(const QString &localPath)
{
    const QString suffix = QFileInfo(localPath).suffix().toLower();
#ifdef HAVE_POPPLER_QT6
    if (suffix == QLatin1String("pdf")) {
        std::unique_ptr<Poppler::Document> doc(Poppler::Document::load(localPath));
        if (!doc || doc->isLocked()) {
            return QPixmap();
        }
        doc->setRenderHint(Poppler::Document::Antialiasing, true);
        doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
        std::unique_ptr<Poppler::Page> page(doc->page(0));
        if (!page) {
            return QPixmap();
        }
        const QSize pageSize = page->pageSize(); // points at 72 dpi
        if (pageSize.width() <= 0) {
            return QPixmap();
        }
        double dpi = 72.0 * coverTargetSize().width() / pageSize.width();
        dpi = qBound(36.0, dpi, 150.0);
        const QImage img = page->renderToImage(dpi, dpi);
        if (img.isNull()) {
            return QPixmap();
        }
        return normalizeCover(QPixmap::fromImage(img));
    }
#else
    Q_UNUSED(suffix);
#endif
    QImageReader reader(localPath);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) {
        return QPixmap();
    }
    return normalizeCover(QPixmap::fromImage(img));
}

void RecentItemsModel::scheduleThumbnails()
{
    ++m_thumbGeneration;
    m_thumbQueue.clear();
    const int gen = m_thumbGeneration;
    for (int row = 0; row < m_recentItems.size(); ++row) {
        // Storage and model rows share one order: newest first.
        RecentItem &item = m_recentItems[row];
        if (item.thumbnail.isNull() && wantsCover(item.url)) {
            // Instant path: reusable disk cache from a previous run.
            const QString cached = coverCachePath(item.url);
            QPixmap hit;
            if (!cached.isEmpty() && hit.load(cached)) {
                item.thumbnail = hit;
            } else {
                m_thumbQueue.append(row);
            }
        }
    }
    if (!m_thumbQueue.isEmpty()) {
        // One cover per event-loop tick: a PDF first-page render can cost
        // ~100ms, so never block the library paint with a batch.
        QTimer::singleShot(0, this, [this, gen] {
            if (gen == m_thumbGeneration) {
                processThumbnailQueue();
            }
        });
    }
}

void RecentItemsModel::processThumbnailQueue()
{
    if (m_thumbQueue.isEmpty()) {
        return;
    }
    const int gen = m_thumbGeneration;
    const int row = m_thumbQueue.takeFirst();
    if (row >= 0 && row < m_recentItems.size()) {
        RecentItem &item = m_recentItems[row];
        if (item.thumbnail.isNull() && wantsCover(item.url)) {
            const QPixmap cover = renderCover(item.url.toLocalFile());
            if (!cover.isNull()) {
                item.thumbnail = cover;
                const QString cached = coverCachePath(item.url);
                if (!cached.isEmpty()) {
                    QDir().mkpath(QFileInfo(cached).absolutePath());
                    cover.save(cached, "PNG");
                }
                const QModelIndex idx = index(row, 0);
                Q_EMIT dataChanged(idx, idx, {ThumbnailRole});
            }
        }
    }
    if (!m_thumbQueue.isEmpty()) {
        QTimer::singleShot(0, this, [this, gen] {
            if (gen == m_thumbGeneration) {
                processThumbnailQueue();
            }
        });
    }
}

#include "moc_recentitemsmodel.cpp"
