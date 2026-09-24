# HL2SDK
# ---

function(BUILD_HL2SDK GAME SDK_PATH)
	add_library(HL2SDK-${GAME} INTERFACE)

	target_include_directories(HL2SDK-${GAME} INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}/${SDK_PATH}/public
		${CMAKE_CURRENT_SOURCE_DIR}/${SDK_PATH}/public/entity2
		${CMAKE_CURRENT_SOURCE_DIR}/${SDK_PATH}/public/mathlib
		${CMAKE_CURRENT_SOURCE_DIR}/${SDK_PATH}/public/tier0
		${CMAKE_CURRENT_SOURCE_DIR}/${SDK_PATH}/public/tier1
	)

	if(WIN32)
		target_link_libraries(HL2SDK-${GAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${SDK_PATH}/lib/public/win64/interfaces.lib)
	else()
		target_link_libraries(HL2SDK-${GAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${SDK_PATH}/lib/linux64/interfaces.a)
	endif()
endfunction()

# DO NOT MODIFY, if you wish to switch the sdk for a game do it in src/main/CMakeLists.txt
build_hl2sdk(CS2 hl2sdk-cs2)
build_hl2sdk(DOTA hl2sdk-dota)
build_hl2sdk(DEADLOCK hl2sdk-deadlock)
build_hl2sdk(HLVR hl2sdk-hlvr)

# EXTERNAL LIBRARIES
# ---

set(SPDLOG_FMT_EXTERNAL ON)

add_subdirectory(fmt)
add_subdirectory(spdlog)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(json)

set_target_properties(
	fmt
	spdlog
	nlohmann_json
	PROPERTIES FOLDER Libraries
)
