add_definitions(-DMETA_IS_SOURCE2 -D_ITERATOR_DEBUG_LEVEL=0)

if(UNIX)
	add_definitions(-D_LINUX -DPOSIX -DLINUX -DGNUC -DCOMPILER_GCC -DPLATFORM_64BITS -D_GLIBCXX_USE_CXX11_ABI=0)
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
elseif(WIN32)
	add_definitions(
		-DCOMPILER_MSVC -DCOMPILER_MSVC64 -DPLATFORM_64BITS -DWIN32 -DWINDOWS -DCRT_SECURE_NO_WARNINGS
		-DCRT_SECURE_NO_DEPRECATE -DCRT_NONSTDC_NO_DEPRECATE -DNOMINMAX
	)

	add_definitions(/MP)
endif()