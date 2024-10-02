# HL2SDK/METAMOD SETUP
# ---

SET(HL2SDK_INCLUDES
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/common
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/game/shared
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/game/server
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/public
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/public/engine
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/public/mathlib
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/public/tier0
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/public/tier1
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/public/entity2
	${PROJECT_SOURCE_DIR}/vendor/hl2sdk/public/game/server
)

add_library(HL2SDK STATIC
	hl2sdk/tier1/convar.cpp
	hl2sdk/tier1/generichash.cpp
	hl2sdk/entity2/entitysystem.cpp
	hl2sdk/entity2/entityidentity.cpp
	hl2sdk/entity2/entitykeyvalues.cpp
	hl2sdk/tier1/keyvalues3.cpp
)

target_include_directories(HL2SDK PUBLIC ${HL2SDK_INCLUDES})

if(WIN32)
	target_link_libraries(HL2SDK PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/hl2sdk/lib/public/win64/tier1.lib
		${CMAKE_CURRENT_SOURCE_DIR}/hl2sdk/lib/public/win64/interfaces.lib
		${CMAKE_CURRENT_SOURCE_DIR}/hl2sdk/lib/public/win64/mathlib.lib
	)
elseif(UNIX)
	target_link_libraries(HL2SDK PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/hl2sdk/lib/linux64/tier1.a
		${CMAKE_CURRENT_SOURCE_DIR}/hl2sdk/lib/linux64/interfaces.a
		${CMAKE_CURRENT_SOURCE_DIR}/hl2sdk/lib/linux64/mathlib.a
	)
endif()

# EXTERNAL LIBRARIES
# ---

#add_subdirectory(json)

set_target_properties(
	HL2SDK
	PROPERTIES FOLDER SDK
)