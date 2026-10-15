# install annotator xml tools description
install(FILES
   tools.xml
   toolsQuick.xml
   drawingtools.xml
   DESTINATION ${KDE_INSTALL_DATADIR}/focus)

# install annotation tool images
install(FILES
   tool-base-focus.png
   tool-base-focus@2x.png
   tool-highlighter-focus-colorizable.png
   tool-highlighter-focus-colorizable@2x.png
   tool-ink-focus-colorizable.png
   tool-ink-focus-colorizable@2x.png
   tool-note.png
   tool-note-focus-colorizable.png
   tool-note-focus-colorizable@2x.png
   tool-note-inline.png
   tool-note-inline-focus-colorizable.png
   tool-note-inline-focus-colorizable@2x.png
   tool-typewriter-focus-colorizable.png
   tool-typewriter-focus-colorizable@2x.png
   DESTINATION ${KDE_INSTALL_DATADIR}/focus/pics)
# install annotation page images
install(FILES
   checkmark.png
   circle.png
   comment.png
   cross.png
   help.png
   insert.png
   key.png
   newparagraph.png
   note.png
   paperclip.png
   paragraph.png
   pushpin.png
   rightarrow.png
   rightpointer.png
   star.png
   uparrow.png
   upleftarrow.png
   DESTINATION ${KDE_INSTALL_DATADIR}/focus/pics)
# install misc images
