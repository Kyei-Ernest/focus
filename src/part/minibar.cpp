/*
    SPDX-FileCopyrightText: 2005 Enrico Ros <eros.kde@email.it>
    SPDX-FileCopyrightText: 2006 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "minibar.h"

// qt / kde includes
#include <KLocalizedString>
#include <QIcon>
#include <QToolButton>
#include <kacceleratormanager.h>
#include <kicontheme.h>
#include <klineedit.h>
#include <qapplication.h>
#include <qevent.h>
#include <qframe.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qpainter.h>
#include <qpushbutton.h>
#include <qtoolbar.h>
#include <qvalidator.h>

// local includes
#include "core/document.h"
#include "core/page.h"

QToolButton *createHoverButton(QWidget *parent)
{
    auto button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    KAcceleratorManager::setNoAccel(button);
    return button;
}

MiniBarLogic::MiniBarLogic(QObject *parent, Okular::Document *document)
    : QObject(parent)
    , m_document(document)
{
}

MiniBarLogic::~MiniBarLogic()
{
    m_document->removeObserver(this);
}

void MiniBarLogic::addMiniBar(MiniBar *miniBar)
{
    m_miniBars.insert(miniBar);
}

void MiniBarLogic::removeMiniBar(MiniBar *miniBar)
{
    m_miniBars.remove(miniBar);
}

Okular::Document *MiniBarLogic::document() const
{
    return m_document;
}

int MiniBarLogic::currentPage() const
{
    return m_document->currentPage();
}

void MiniBarLogic::notifySetup(const QList<Okular::Page *> &pageVector, int setupFlags)
{
    // only process data when document changes
    if (!(setupFlags & Okular::DocumentObserver::DocumentChanged)) {
        return;
    }

    // if document is closed or has no pages, hide widget
    const int pages = pageVector.count();
    if (pages < 1) {
        for (MiniBar *miniBar : std::as_const(m_miniBars)) {
            miniBar->setEnabled(false);
        }
        return;
    }

    bool labelsDiffer = false;
    for (const Okular::Page *page : pageVector) {
        if (!page->label().isEmpty()) {
            if (page->label().toInt() != (page->number() + 1)) {
                labelsDiffer = true;
            }
        }
    }

    const QString pagesString = QString::number(pages);

    // In some documents, there may be labels which are longer than pagesString. Here, we check all the page labels, and if any of the labels are longer than pagesString, we use that string for sizing m_pageLabelEdit
    QString pagesOrLabelString = pagesString;
    if (labelsDiffer) {
        for (const Okular::Page *page : pageVector) {
            if (!page->label().isEmpty()) {
                MiniBar *miniBar = *m_miniBars.constBegin(); // We assume all the minibars have the same font, font size etc, so we just take one minibar for the purpose of calculating the displayed length of the page labels.
                if (miniBar->fontMetrics().horizontalAdvance(page->label()) > miniBar->fontMetrics().horizontalAdvance(pagesOrLabelString)) {
                    pagesOrLabelString = page->label();
                }
            }
        }
    }

    for (MiniBar *miniBar : std::as_const(m_miniBars)) {
        // resize width of widgets
        miniBar->resizeForPage(pages, pagesOrLabelString);

        // update child widgets
        miniBar->m_pageLabelEdit->setPageLabels(pageVector);
        miniBar->m_pageNumberEdit->setPagesNumber(pages);
        miniBar->m_totalLabel->setText(i18nc("Page counter, e.g. 'of 10'", "of %1", pagesString));
        miniBar->m_prevButton->setEnabled(false);
        miniBar->m_nextButton->setEnabled(false);
        miniBar->m_pageLabelEdit->setVisible(labelsDiffer);
        miniBar->m_pageNumberEdit->setVisible(!labelsDiffer);

        miniBar->adjustSize();

        miniBar->setEnabled(true);
    }
}

void MiniBarLogic::notifyCurrentPageChanged(int previousPage, int currentPage)
{
    Q_UNUSED(previousPage)

    // get current page number
    const int pages = m_document->pages();

    // if the document is opened and page is changed
    if (pages > 0) {
        const QString pageNumber = QString::number(currentPage + 1);
        const QString pageLabel = m_document->page(currentPage)->label();

        for (MiniBar *miniBar : std::as_const(m_miniBars)) {
            // update prev/next button state
            miniBar->m_prevButton->setEnabled(currentPage > 0);
            miniBar->m_nextButton->setEnabled(currentPage < (pages - 1));
            // update text on widgets
            miniBar->m_pageNumberEdit->setText(pageNumber);
            miniBar->m_pageLabelEdit->setText(pageLabel);
            // When custom labels are used the field shows the label; keep
            // the numeric page available as a tooltip.
            miniBar->m_pageLabelEdit->setToolTip(pageNumber);
        }
    }
}

/** MiniBar **/

MiniBar::MiniBar(QWidget *parent, MiniBarLogic *miniBarLogic)
    : QWidget(parent)
    , m_miniBarLogic(miniBarLogic)
    , m_oldToolbarParent(nullptr)
{
    setObjectName(QStringLiteral("miniBar"));

    m_miniBarLogic->addMiniBar(this);

    QHBoxLayout *horLayout = new QHBoxLayout(this);

    horLayout->setContentsMargins(0, 0, 0, 0);
    horLayout->setSpacing(5);

    // One icon size for every button on the bar.
    const QSize buttonSize(20, 20);
    // Compact page navigator: [prev] [page field] "of N" [next] on one
    // baseline, every child vertically centered.
    m_prevButton = createHoverButton(this);
    m_prevButton->setIcon(QIcon::fromTheme(QStringLiteral("go-previous"), QIcon::fromTheme(QStringLiteral("arrow-left"))));
    m_prevButton->setIconSize(buttonSize);
    m_prevButton->setToolTip(i18n("Go to the previous page"));
    horLayout->addWidget(m_prevButton, 0, Qt::AlignVCenter);
    // bottom: left lineEdit (current page box)
    m_pageNumberEdit = new PageNumberEdit(this);
    horLayout->addWidget(m_pageNumberEdit, 0, Qt::AlignVCenter);
    m_pageNumberEdit->installEventFilter(this);
    // bottom: left labelWidget (current page label, for documents with
    // custom page labels)
    m_pageLabelEdit = new PageLabelEdit(this);
    horLayout->addWidget(m_pageLabelEdit, 0, Qt::AlignVCenter);
    m_pageLabelEdit->installEventFilter(this);
    // Single "of N" label in the muted text color (palette-driven).
    m_totalLabel = new QLabel(this);
    m_totalLabel->setAlignment(Qt::AlignCenter);
    m_totalLabel->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); background: transparent; border: none; }"));
    horLayout->addWidget(m_totalLabel, 0, Qt::AlignVCenter);
    // bottom: right next_page button
    m_nextButton = createHoverButton(this);
    m_nextButton->setIcon(QIcon::fromTheme(QStringLiteral("go-next"), QIcon::fromTheme(QStringLiteral("arrow-right"))));
    m_nextButton->setIconSize(buttonSize);
    m_nextButton->setToolTip(i18n("Go to the next page"));
    horLayout->addWidget(m_nextButton, 0, Qt::AlignVCenter);

    // Subtle palette-following field: blends with the toolbar when idle,
    // uses the native edit color only while focused. Never a bright white box.
    const QString fieldStyle = QStringLiteral("QLineEdit { background: palette(window); border: 1px solid palette(mid); border-radius: 4px; padding: 0 6px; color: palette(window-text); selection-background-color: palette(highlight); selection-color: palette(highlighted-text); } "
                                              "QLineEdit:focus { background: palette(base); border-color: palette(highlight); }");
    m_pageNumberEdit->setStyleSheet(fieldStyle);
    m_pageLabelEdit->setStyleSheet(fieldStyle);

    // Same height for the field and the buttons (derived from the style).
    const int uniformHeight = qMax(m_prevButton->sizeHint().height(), m_pageNumberEdit->sizeHint().height());
    m_prevButton->setFixedHeight(uniformHeight);
    m_nextButton->setFixedHeight(uniformHeight);
    m_pageNumberEdit->setFixedHeight(uniformHeight);
    m_pageLabelEdit->setFixedHeight(uniformHeight);

    QSizePolicy sp = sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Fixed);
    sp.setVerticalPolicy(QSizePolicy::Fixed);
    setSizePolicy(sp);

    // resize width of widgets
    resizeForPage(0, QString());

    // connect signals from child widgets to internal handlers / signals bouncers
    connect(m_pageNumberEdit, &PageNumberEdit::returnKeyPressed, this, &MiniBar::slotChangePageFromReturn);
    connect(m_pageLabelEdit, &PageLabelEdit::pageNumberChosen, this, &MiniBar::slotChangePage);
    connect(m_prevButton, &QAbstractButton::clicked, this, &MiniBar::prevPage);
    connect(m_nextButton, &QAbstractButton::clicked, this, &MiniBar::nextPage);

    adjustSize();

    // widget starts disabled (will be enabled after opening a document)
    setEnabled(false);
}

MiniBar::~MiniBar()
{
    m_miniBarLogic->removeMiniBar(this);
}

void MiniBar::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ParentChange) {
        QToolBar *tb = dynamic_cast<QToolBar *>(parent());
        if (tb != m_oldToolbarParent) {
            if (m_oldToolbarParent) {
                disconnect(m_oldToolbarParent, &QToolBar::iconSizeChanged, this, &MiniBar::slotToolBarIconSizeChanged);
            }
            m_oldToolbarParent = tb;
            if (tb) {
                connect(tb, &QToolBar::iconSizeChanged, this, &MiniBar::slotToolBarIconSizeChanged);
                slotToolBarIconSizeChanged();
            }
        }
    }
}

bool MiniBar::eventFilter(QObject *target, QEvent *event)
{
    if (target == m_pageNumberEdit || target == m_pageLabelEdit) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            int key = keyEvent->key();
            if (key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_Up || key == Qt::Key_Down) {
                Q_EMIT forwardKeyPressEvent(keyEvent);
                return true;
            }
        }
    }
    return false;
}

void MiniBar::slotChangePageFromReturn()
{
    // get text from the lineEdit
    const QString pageNumber = m_pageNumberEdit->text();

    // convert it to page number and go to that page
    bool ok;
    int number = pageNumber.toInt(&ok) - 1;
    if (ok && number >= 0 && number < (int)m_miniBarLogic->document()->pages() && number != m_miniBarLogic->currentPage()) {
        slotChangePage(number);
    }
}

void MiniBar::slotChangePage(int pageNumber)
{
    m_miniBarLogic->document()->setViewportPage(pageNumber);
    m_pageNumberEdit->clearFocus();
    m_pageLabelEdit->clearFocus();
}

void MiniBar::slotEmitNextPage()
{
    // Q_EMIT signal
    Q_EMIT nextPage();
}

void MiniBar::slotEmitPrevPage()
{
    // Q_EMIT signal
    Q_EMIT prevPage();
}

void MiniBar::slotToolBarIconSizeChanged()
{
    const QSize buttonSize = m_oldToolbarParent->iconSize();
    m_prevButton->setIconSize(buttonSize);
    m_nextButton->setIconSize(buttonSize);
    // Keep the field the same height as the buttons.
    const int uniformHeight = qMax(m_prevButton->sizeHint().height(), m_pageNumberEdit->sizeHint().height());
    m_prevButton->setFixedHeight(uniformHeight);
    m_nextButton->setFixedHeight(uniformHeight);
    m_pageNumberEdit->setFixedHeight(uniformHeight);
    m_pageLabelEdit->setFixedHeight(uniformHeight);
}

void MiniBar::resizeForPage(int pages, const QString &pagesOrLabelString)
{
    Q_UNUSED(pagesOrLabelString);
    // Fixed width for about 4 digits so the field never pushes other
    // controls around; grown only for very large documents.
    const int fourDigits = fontMetrics().horizontalAdvance(QStringLiteral("0000"));
    const int needDigits = fontMetrics().horizontalAdvance(QString::number(qMax(pages, 1)));
    const int frame = 16; // cursor, frame and inner padding allowance
    const int fieldWidth = qMax(fourDigits, needDigits) + frame;
    m_pageNumberEdit->setFixedWidth(fieldWidth);
    m_pageLabelEdit->setFixedWidth(fieldWidth);
}

/** PageLabelEdit **/

PageLabelEdit::PageLabelEdit(MiniBar *parent)
    : PagesEdit(parent)
{
    setVisible(false);
    connect(this, &PageLabelEdit::returnKeyPressed, this, &PageLabelEdit::pageChosen);
}

void PageLabelEdit::setText(const QString &newText)
{
    m_lastLabel = newText;
    PagesEdit::setText(newText);
}

void PageLabelEdit::setPageLabels(const QList<Okular::Page *> &pageVector)
{
    m_labelPageMap.clear();
    completionObject()->clear();
    for (const Okular::Page *page : pageVector) {
        if (!page->label().isEmpty()) {
            m_labelPageMap.insert(page->label(), page->number());
            bool ok;
            page->label().toInt(&ok);
            if (!ok) {
                // Only add to the completion objects labels that are not numbers
                completionObject()->addItem(page->label());
            }
        }
    }
}

void PageLabelEdit::pageChosen()
{
    const QString newInput = text();
    const int pageNumber = m_labelPageMap.value(newInput, -1);
    if (pageNumber != -1) {
        Q_EMIT pageNumberChosen(pageNumber);
    } else {
        setText(m_lastLabel);
    }
}

/** PageNumberEdit **/

PageNumberEdit::PageNumberEdit(MiniBar *miniBar)
    : PagesEdit(miniBar)
{
    // use an integer validator
    m_validator = new QIntValidator(1, 1, this);
    setValidator(m_validator);
}

void PageNumberEdit::setPagesNumber(int pages)
{
    m_validator->setTop(pages);
}

/** PagesEdit **/

PagesEdit::PagesEdit(MiniBar *parent)
    : KLineEdit(parent)
    , m_miniBar(parent)
    , m_eatClick(false)
{
    // customize text properties
    setAlignment(Qt::AlignCenter);

    // send a focus out event
    QFocusEvent fe(QEvent::FocusOut);
    QApplication::sendEvent(this, &fe);
    qApp->installEventFilter(this);
}

void PagesEdit::setText(const QString &newText)
{
    // call default handler if hasn't focus
    if (!hasFocus()) {
        KLineEdit::setText(newText);
    }
    // else preserve existing selection
    else {
        // save selection and adapt it to the new text length
        int selectionLength = selectedText().length();
        const bool allSelected = (selectionLength == text().length());
        if (allSelected) {
            KLineEdit::setText(newText);
            selectAll();
        } else {
            int newSelectionStart = newText.length() - text().length() + selectionStart();
            if (newSelectionStart < 0) {
                // the new text is shorter than the old one, and the front part, which is "cut off", is selected
                // shorten the selection accordingly
                selectionLength += newSelectionStart;
                newSelectionStart = 0;
            }
            KLineEdit::setText(newText);
            setSelection(newSelectionStart, selectionLength);
        }
    }
}

void PagesEdit::updatePalette()
{
    QPalette pal;

    if (hasFocus()) {
        pal.setColor(QPalette::Active, QPalette::Base, QApplication::palette().color(QPalette::Active, QPalette::Base));
    } else {
        pal.setColor(QPalette::Base, QApplication::palette().color(QPalette::Base).darker(102));
    }

    setPalette(pal);
}

bool PagesEdit::eventFilter(QObject *watched, QEvent *e)
{
    if (watched == qApp && e->type() == QEvent::ApplicationPaletteChange) {
        updatePalette();
    }
    return KLineEdit::eventFilter(watched, e);
}

void PagesEdit::focusInEvent(QFocusEvent *e)
{
    // select all text
    selectAll();
    if (e->reason() == Qt::MouseFocusReason) {
        m_eatClick = true;
    }
    // change background color to the default 'edit' color
    updatePalette();
    // call default handler
    KLineEdit::focusInEvent(e);
}

void PagesEdit::focusOutEvent(QFocusEvent *e)
{
    // change background color to a dark tone
    updatePalette();
    // call default handler
    KLineEdit::focusOutEvent(e);
}

void PagesEdit::mousePressEvent(QMouseEvent *e)
{
    // if this click got the focus in, don't process the event
    if (!m_eatClick) {
        KLineEdit::mousePressEvent(e);
    }
    m_eatClick = false;
}

void PagesEdit::wheelEvent(QWheelEvent *e)
{
    if (e->angleDelta().y() > 0) {
        m_miniBar->slotEmitNextPage();
    } else {
        m_miniBar->slotEmitPrevPage();
    }
}

#include "moc_minibar.cpp"
