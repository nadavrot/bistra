file(GLOB files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.m)
foreach(filename ${files})
  configure_file(${filename} ${CMAKE_CURRENT_BINARY_DIR}/${filename} COPYONLY)
endforeach(filename)

install(FILES ${files} DESTINATION examples)

