file(GLOB_RECURSE SOURCES_LIST
  "*.cpp"
)

file(GLOB_RECURSE HEADERS_LIST
  "*.h"
)

add_executable(CS2Dumper ${SOURCES_LIST} ${HEADERS_LIST})

target_link_libraries(CS2Dumper PRIVATE
  HL2SDK
  #Protobufs
  #nlohmann_json
  ${CMAKE_DL_LIBS}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCES_LIST})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADERS_LIST})