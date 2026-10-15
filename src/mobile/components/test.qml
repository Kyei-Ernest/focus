/*
    SPDX-FileCopyrightText: 2012 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.kde.focus 2.0 as Okular

Item {
    width: 500
    height: 600
    Focus.DocumentItem {
        id: docItem
        url: Qt.resolvedUrl("pageitem.cpp")
    }
    Focus.PageItem {
        id: page
        anchors.fill: parent
        document: docItem
    }
    Row {
        anchors {
            bottom: parent.bottom
            right: parent.right
        }
        Button {
            text: "prev"
            onClicked: page.pageNumber--
        }
        Button {
            text: "next"
            onClicked: page.pageNumber++
        }
    }
}
