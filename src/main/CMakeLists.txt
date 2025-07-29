file(GLOB_RECURSE SOURCES_LIST
  "*.cpp"
)

file(GLOB_RECURSE HEADERS_LIST
  "*.h"
)

function(BUILD_GAME GAME GAME_PATH SDK)
  add_executable(DumpSource2-${GAME} ${SOURCES_LIST} ${HEADERS_LIST})

  target_link_libraries(DumpSource2-${GAME} PRIVATE
    HL2SDK-${SDK}
    fmt::fmt
    spdlog
    ${CMAKE_DL_LIBS}
  )

  target_compile_definitions(DumpSource2-${GAME} PRIVATE GAME_${GAME} GAME_PATH="${GAME_PATH}")
endfunction()

build_game(CS2 csgo DEADLOCK)
build_game(DOTA dota DOTA)
build_game(DEADLOCK citadel DEADLOCK)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCES_LIST})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADERS_LIST})