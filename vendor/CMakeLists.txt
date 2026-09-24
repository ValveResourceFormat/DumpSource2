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
			${GAME_PATH}/tier1/keyvalues3.cpp
		)

		# Newer SDKs made tier1 header only
		if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/tier1/generichash.cpp)
			target_sources(HL2SDK-${GAME} PRIVATE ${GAME_PATH}/tier1/generichash.cpp)
		endif()

		target_include_directories(HL2SDK-${GAME} PUBLIC ${HL2SDK_INCLUDES})

		if(WIN32)
			set(HL2SDK_LIBS ${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/lib/public/win64)
			set(HL2SDK_LIB_EXT lib)
		elseif(UNIX)
			set(HL2SDK_LIBS ${CMAKE_CURRENT_SOURCE_DIR}/${GAME_PATH}/lib/linux64)
			set(HL2SDK_LIB_EXT a)
		endif()

		target_link_libraries(HL2SDK-${GAME} PUBLIC
			${HL2SDK_LIBS}/interfaces.${HL2SDK_LIB_EXT}
			${HL2SDK_LIBS}/mathlib.${HL2SDK_LIB_EXT}
		)

		if(EXISTS ${HL2SDK_LIBS}/tier1.${HL2SDK_LIB_EXT})
			target_link_libraries(HL2SDK-${GAME} PUBLIC ${HL2SDK_LIBS}/tier1.${HL2SDK_LIB_EXT})
		endif()
		message(STATUS "Setting up HL2SDK-${GAME}") # What does this print?
endfunction()

# DO NOT MODIFY, if you wish to switch the sdk for a game do it in src/main/CMakeLists.tst
build_hl2sdk(CS2 hl2sdk-cs2)
build_hl2sdk(DOTA hl2sdk-dota)
build_hl2sdk(DEADLOCK hl2sdk-deadlock)

# EXTERNAL LIBRARIES
# ---

set(SPDLOG_FMT_EXTERNAL ON)

add_subdirectory(fmt)
add_subdirectory(spdlog)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(json)

set_target_properties(
	HL2SDK-CS2
	HL2SDK-DOTA
	HL2SDK-DEADLOCK
	PROPERTIES FOLDER SDK
)

set_target_properties(
	fmt
	spdlog
	nlohmann_json
	PROPERTIES FOLDER Libraries
)