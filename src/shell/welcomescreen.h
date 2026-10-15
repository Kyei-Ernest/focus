/*
    SPDX-FileCopyrightText: 2021 Jiří Wolker <woljiri@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef WELCOMESCREEN_H
#define WELCOMESCREEN_H

#include "shell/ui_welcomescreen.h"

#include <QFrame>
#include <QUrl>

class KRecentFilesAction;
class QLabel;
class QListWidgetItem;
class QButtonGroup;
class QSortFilterProxyModel;
class RecentItemsModel;
class RecentsListItemDelegate;

class WelcomeScreen : public QWidget, Ui::WelcomeScreen
{
    Q_OBJECT
public:
    explicit WelcomeScreen(QWidget *parent = nullptr);
    ~WelcomeScreen() override;

    void loadRecents();
    void setMaxRecentItems(const int maxItems);
    // Record a successful open in the library history (newest first).
    void noteDocumentOpened(const QUrl &url);
    // Drop url from the library history (context-menu "Forget").
    void forgetLibraryItem(const QUrl &url);
    bool isGridViewMode() const;

Q_SIGNALS:
    void openClicked();
    void closeClicked();
    void recentItemClicked(QUrl const &url);
    void forgetRecentItem(QUrl const &url);

protected:
    void paintEvent(QPaintEvent *e) override;

private Q_SLOTS:
    void recentsItemActivated(QModelIndex const &index);
    void recentListChanged();
    void onSearchTextChanged(const QString &text);
    void onFilterButtonClicked();
    void onSortChanged(int row);
    void onViewModeToggled(bool grid);

private:
    int recentsCount();
    void updateEmptyState();
    void updateListHeaderMargin();
    QString currentFilterCategory() const;

    RecentItemsModel *m_recentsModel;
    QSortFilterProxyModel *m_proxyModel;
    QButtonGroup *m_filterGroup;
    RecentsListItemDelegate *m_recentsItemDelegate;
    // Flat-table header for list mode (Name | Size | Modified); hidden in
    // grid mode. Column metrics mirror the delegate's list-row constants.
    QWidget *m_listHeader = nullptr;
    QLabel *m_nameHeader = nullptr;
    QLabel *m_sizeHeader = nullptr;
    QLabel *m_modHeader = nullptr;
};

#endif // WELCOMESCREEN_H
