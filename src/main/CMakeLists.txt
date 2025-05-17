file(GLOB_RECURSE SOURCES_LIST
  "*.cpp"
)

file(GLOB_RECURSE HEADERS_LIST
  "*.h"
)

function(BUILD_GAME GAME GAME_PATH)
  add_executable(DumpSource2-${GAME} ${SOURCES_LIST} ${HEADERS_LIST})

  target_link_libraries(DumpSource2-${GAME} PRIVATE
    HL2SDK-${GAME}
    fmt::fmt
    ${CMAKE_DL_LIBS}
  )

  target_compile_definitions(DumpSource2-${GAME} PRIVATE GAME_${GAME} GAME_PATH="${GAME_PATH}")
endfunction()

build_game(CS2 csgo)
build_game(DOTA dota)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCES_LIST})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADERS_LIST})