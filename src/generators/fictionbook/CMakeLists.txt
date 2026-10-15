remove_definitions(-DTRANSLATION_DOMAIN="focus")
add_definitions(-DTRANSLATION_DOMAIN="focus_fictionbook")

########### next target ###############

set(okularGenerator_fb_PART_SRCS
  converter.cpp
  document.cpp
  generator_fb.cpp
)


focus_add_generator(okularGenerator_fb ${okularGenerator_fb_PART_SRCS})

target_link_libraries(okularGenerator_fb okularcore KF6::Archive KF6::I18n Qt6::Xml)

########### install files ###############
if (BUILD_DESKTOP)
  install( PROGRAMS focusApplication_fb.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
if (BUILD_MOBILE)
  install( PROGRAMS org.kde.mobile.focus_fb.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
install( FILES org.kde.focus-fb.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )

ecm_install_icons(ICONS 16-apps-focus-fb2.png  32-apps-focus-fb2.png  48-apps-focus-fb2.png DESTINATION ${KDE_INSTALL_DATADIR}/focus/icons THEME hicolor)
