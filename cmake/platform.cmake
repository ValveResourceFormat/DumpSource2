if(UNIX)
	add_compile_definitions(
		_LINUX POSIX LINUX GNUC COMPILER_GCC PLATFORM_64BITS _GLIBCXX_USE_CXX11_ABI=0 stricmp=strcasecmp
		_stricmp=strcasecmp _snprintf=snprintf _vsnprintf=vsnprintf HAVE_STDINT_H X64BITS
	)
elseif(WIN32)
	add_compile_definitions(COMPILER_MSVC COMPILER_MSVC64 PLATFORM_64BITS WIN32 WINDOWS NOMINMAX X64BITS _ITERATOR_DEBUG_LEVEL=0)
	add_compile_options(/MP)
endif()
