# SPDX-FileCopyrightText: 2024 Carl Schwan <carl@carlschwan.eu>
# SPDX-License-Identifier: BSD-2-Clause

ecm_add_qml_module(focusplugin 
    URI "org.kde.focus"
    VERSION 2.0
    GENERATE_PLUGIN_SOURCE
    DEPENDENCIES
        org.kde.kirigami
)

target_sources(focusplugin PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/guiutils.cpp
    ${CMAKE_SOURCE_DIR}/gui/tocmodel.cpp
    ${CMAKE_SOURCE_DIR}/gui/signaturemodel.cpp
    ${CMAKE_SOURCE_DIR}/gui/signatureguiutils.cpp
    ${CMAKE_SOURCE_DIR}/gui/certificatemodel.cpp
    ${CMAKE_SOURCE_DIR}/gui/pagepainter.cpp
    ${CMAKE_SOURCE_DIR}/gui/debug_ui.cpp
    ${CMAKE_SOURCE_DIR}/gui/recentitemsmodel.cpp
    pageitem.cpp
    documentitem.cpp
    thumbnailitem.cpp
    okularsingleton.cpp
    types.cpp
    welcomeitem.cpp
)

kconfig_add_kcfg_files(focusplugin GENERATE_MOC ${CMAKE_SOURCE_DIR}/conf/settings_mobile.kcfgc)

ecm_target_qml_sources(focusplugin SOURCES
    DocumentView.qml
)

ecm_target_qml_sources(focusplugin PRIVATE PATH private SOURCES
    private/PageView.qml
)

target_link_libraries(focusplugin PRIVATE
    Qt6::Quick
    Qt6::Qml
    Qt6::Core
    Qt6::Xml
    Qt6::Svg
    KF6::Bookmarks
    KF6::I18n
    KF6::Service
    okularcore
)

ecm_finalize_qml_module(focusplugin DESTINATION ${KDE_INSTALL_QMLDIR} EXPORT FocusTargets)

#add_subdirectory(test)
