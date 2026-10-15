remove_definitions(-DTRANSLATION_DOMAIN="focus")
add_definitions(-DTRANSLATION_DOMAIN="focus_ghostview")

add_subdirectory( conf )

########### next target ###############

set(okularGenerator_ghostview_SRCS
   generator_ghostview.cpp
   rendererthread.cpp
   spectre_debug.cpp
)

ki18n_wrap_ui(okularGenerator_ghostview_SRCS
   conf/gssettingswidget.ui
)

kconfig_add_kcfg_files(okularGenerator_ghostview_SRCS conf/gssettings.kcfgc )


focus_add_generator(okularGenerator_ghostview ${okularGenerator_ghostview_SRCS})

target_include_directories(okularGenerator_ghostview PRIVATE ${LIBSPECTRE_INCLUDE_DIR})
target_link_libraries(okularGenerator_ghostview okularcore ${LIBSPECTRE_LIBRARY} KF6::I18n Qt6::Xml)

########### install files ###############

if (BUILD_DESKTOP)
  install( PROGRAMS focusApplication_ghostview.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
if (BUILD_MOBILE)
  install( PROGRAMS org.kde.mobile.focus_ghostview.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
ecm_install_icons(ICONS 16-apps-focus-gv.png  32-apps-focus-gv.png DESTINATION ${KDE_INSTALL_DATADIR}/focus/icons)
install( FILES org.kde.focus-spectre.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
