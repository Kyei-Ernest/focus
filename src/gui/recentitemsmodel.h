/*
    SPDX-FileCopyrightText: 2021 Jiří Wolker <woljiri@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef RECENTITEMSMODEL_H
#define RECENTITEMSMODEL_H

#include <QAbstractListModel>
#include <QFileIconProvider>
#include <QList>
#include <QModelIndex>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QUrl>

class KConfigGroup;

/**
 * @todo write docs
 */
class RecentItemsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles { IconNameRole = Qt::UserRole + 1, UrlRole = Qt::UserRole + 2, FormatRole = Qt::UserRole + 3, SizeRole = Qt::UserRole + 4, BytesRole = Qt::UserRole + 5, ThumbnailRole = Qt::UserRole + 6, ModifiedRole = Qt::UserRole + 7 };
    struct RecentItem {
        QString name;
        QUrl url;
        // First-page cover (PDFs) or image content (IMG), rendered lazily
        // after loadEntries(); null while pending or unsupported.
        QPixmap thumbnail;
    };

    /**
     * Default constructor
     */
    RecentItemsModel();

    /**
     * Destructor
     */
    ~RecentItemsModel() override;

    void loadEntries(const KConfigGroup &cg);
    void clearEntries();
    int maxItems();
    void setMaxItems(const int maxItems);

    RecentItemsModel::RecentItem const *getItem(const QModelIndex &) const;
    RecentItemsModel::RecentItem const *getItem(int index) const;
    // Newest entry (File1 in the config), used by the Continue strip.
    RecentItemsModel::RecentItem const *mostRecentItem() const;

    // JotaLab-inspired library helpers: format badge + human size.
    static QString formatForUrl(const QUrl &url);
    static QString sizeForUrl(const QUrl &url);
    static QString displayNameForItem(const RecentItem &item);

    // Model implementation:
    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::ItemDataRole::DisplayRole) const override;

    // Library history: newest-first, persisted in the "LibraryFiles" config
    // group, capped well above the File-menu recents so old documents stay
    // in the (scrollable) library instead of aging out of it.
    static constexpr int kLibraryMaxItems = 200;
    void noteOpened(const QUrl &url);
    void removeUrl(const QUrl &url);

private Q_SLOTS:
    void processThumbnailQueue();

private:
    void saveLibrary() const;
    // Cover target (2x the 112x148 grid cover for crispness).
    static QSize coverTargetSize();
    static bool wantsCover(const QUrl &url);
    static QString coverCachePath(const QUrl &url);
    static QPixmap renderCover(const QString &localPath);
    static QPixmap normalizeCover(const QPixmap &src);
    void scheduleThumbnails();
    QList<RecentItemsModel::RecentItem> m_recentItems;
    // m_maxItems only affects the recentListsView on the welcome screen. The no. of recent files in the File menu are not affected.
    int m_maxItems = 20;
    QFileIconProvider m_iconProvider;
    // Rows (model indexes) still waiting for a lazily rendered cover.
    QList<int> m_thumbQueue;
    // Bumped on every loadEntries()/clearEntries() so queued renders from a
    // replaced item list never store into the new one.
    int m_thumbGeneration = 0;
};

#endif // RECENTITEMSMODEL_H
