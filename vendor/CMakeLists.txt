# HL2SDK/METAMOD SETUP
# ---

function(BUILD_HL2SDK GAME GAME_PATH)
		SET(HL2SDK_INCLUDES
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/common
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/game/shared
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/game/server
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/public
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/public/engine
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/public/mathlib
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/public/tier0
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/public/tier1
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/public/entity2
			${PROJECT_SOURCE_DIR}/vendor/${GAME_PATH}/public/game/server
		)

		add_library(HL2SDK-${GAME} STATIC
			${GAME_PATH}/tier1/convar.cpp
			${GAME_PATH}/tier1/generichash.cpp
			${GAME_PATH}/tier1/keyvalues3.cpp
		)

		target_include_directories(HL2SDK-${GAME} PUBLIC ${HL2SDK_INCLUDES})

		if(WIN32)
			target_link_libraries(HL2SDK-${GAME} PUBLIC
				${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/lib/public/win64/tier1.lib
				${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/lib/public/win64/interfaces.lib
				${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/lib/public/win64/mathlib.lib
			)
		elseif(UNIX)
			target_link_libraries(HL2SDK-${GAME} PUBLIC
				${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/lib/linux64/tier1.a
				${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/lib/linux64/interfaces.a
				${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/lib/linux64/mathlib.a
			)
		endif()
		message(STATUS "Setting up HL2SDK-${GAME}") # What does this print?
endfunction()

build_hl2sdk(CS2 hl2sdk-cs2)
build_hl2sdk(DOTA hl2sdk-cs2)
build_hl2sdk(DEADLOCK hl2sdk-cs2)

# EXTERNAL LIBRARIES
# ---

set(SPDLOG_FMT_EXTERNAL ON)

add_subdirectory(fmt)
add_subdirectory(spdlog)

set_target_properties(
	HL2SDK-CS2
	HL2SDK-DOTA
	HL2SDK-DEADLOCK
	PROPERTIES FOLDER SDK
)

set_target_properties(
	fmt
	spdlog
	PROPERTIES FOLDER Libraries
)