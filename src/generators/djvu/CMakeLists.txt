remove_definitions(-DTRANSLATION_DOMAIN="focus")
add_definitions(-DTRANSLATION_DOMAIN="focus_djvu")

########### next target ###############

set(okularGenerator_djvu_SRCS
   generator_djvu.cpp
   kdjvu.cpp
)


focus_add_generator(okularGenerator_djvu ${okularGenerator_djvu_SRCS})

target_include_directories(okularGenerator_djvu PRIVATE ${DJVULIBRE_INCLUDE_DIR})
target_link_libraries(okularGenerator_djvu okularcore ${DJVULIBRE_LIBRARY} KF6::I18n)

########### install files ###############
if (BUILD_DESKTOP)
  install( PROGRAMS focusApplication_djvu.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
if (BUILD_MOBILE)
  install( PROGRAMS org.kde.mobile.focus_djvu.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
install( FILES org.kde.focus-djvu.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )

