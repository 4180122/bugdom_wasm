cmake_minimum_required(VERSION 3.21)

set(GAME_TARGET				"Bugdom")
set(GAME_FULL_NAME			"Bugdom")
set(GAME_VERSION			"1.3.5")
set(GAME_IDENTIFIER			"io.jor.bugdom")
set(GAME_COPYRIGHT			"© 1999 Pangea Software, Inc., © 2025 Iliyas Jorio.")

set(GAME_DATADIR			"${CMAKE_SOURCE_DIR}/Data")
set(GAME_SRCDIR				"${CMAKE_SOURCE_DIR}/src")

set(CMAKE_CXX_STANDARD		20)
set(CMAKE_C_STANDARD		17)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")  # Sync with LSMinimumSystemVersion in SDL3.framework's Info.plist
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target macOS architectures")

project(${GAME_TARGET}
	VERSION ${GAME_VERSION}
	LANGUAGES C CXX)

# Emscripten: set legacy GL emulation early so -lGL resolves to the emu library.
# Bugdom uses fixed-function OpenGL (glVertex3f, glLoadMatrixf, glLightfv, etc.)
# which WebGL 2 doesn't provide natively.
if(EMSCRIPTEN)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sLEGACY_GL_EMULATION -sGL_FFP_ONLY=1")
endif()

option(BUILD_SDL_FROM_SOURCE "Build SDL from source" OFF)
option(SDL_STATIC "Static link SDL" OFF)

option(SANITIZE "Build with asan/ubsan" OFF)

if(WIN32 OR APPLE)
	# Don't warn
elseif(SANITIZE)
	message("Sanitizers enabled")
else()
	message("Sanitizers disabled (pass -DSANITIZE=1 to enable)")
endif()

#------------------------------------------------------------------------------
# GLOBAL OPTIONS (BEFORE ADDING SUBDIRECTORIES)
#------------------------------------------------------------------------------

if(MSVC)
	# This lets us detect that MSVC supports C++20 features via the __cplusplus macro
	# (See: https://docs.microsoft.com/en-us/cpp/build/reference/zc-cplusplus)
	add_compile_options(/Zc:__cplusplus)
	add_compile_definitions(UNICODE _UNICODE)

	set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${GAME_TARGET})
elseif(APPLE)
	# Let the game (and Pomme) find header files within extern/SDL3.framework
	set(CMAKE_XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS extern)
	add_compile_options("-F${CMAKE_SOURCE_DIR}/extern")  # for non-xcode IDEs
endif()

set(POMME_NO_GRAPHICS	true)
set(POMME_NO_INPUT		true)
set(POMME_NO_MP3		true)
set(POMME_NO_QD3D		false)		# Bugdom needs this
set(POMME_NO_VIDEO		true)
add_compile_definitions(POMME_CASE_SENSITIVE_FSSPEC=1)

#------------------------------------------------------------------------------
# FIND SDL
#------------------------------------------------------------------------------

# Emscripten/WASM: must build SDL from source (no pre-built web SDL package)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
	set(BUILD_SDL_FROM_SOURCE ON CACHE BOOL "Build SDL from source (required for Emscripten)" FORCE)
	if(NOT DEFINED SDL3_DIR)
		set(SDL3_DIR "${CMAKE_SOURCE_DIR}/extern/SDL")
	endif()
	# -O3 can hurt performance on legacy GL emulation (Debug -O0 runs faster in practice).
	# Use -O2 for Release; override via -DWASM_RELEASE_OPT=3 if you want full optimization.
	set(WASM_RELEASE_OPT "2" CACHE STRING "Emscripten Release optimization: 0,1,2,3,s,z,g (default 2)")
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		set(CMAKE_C_FLAGS_RELEASE "-O${WASM_RELEASE_OPT} -DNDEBUG" CACHE STRING "" FORCE)
		set(CMAKE_CXX_FLAGS_RELEASE "-O${WASM_RELEASE_OPT} -DNDEBUG" CACHE STRING "" FORCE)
		message(STATUS "WASM Release: using -O${WASM_RELEASE_OPT} (override with -DWASM_RELEASE_OPT=3 for -O3)")
	elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
		# Debug uses -O2 -g: keeps symbols for stack traces but optimizes for speed (-O0 is very slow)
		# set(CMAKE_C_FLAGS_DEBUG "-O2 -g" CACHE STRING "" FORCE)
		# set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g" CACHE STRING "" FORCE)
		message(STATUS "WASM Debug: using -O2 -g (optimized but with debug symbols)")
	endif()
endif()

# If SDL3_DIR wasn't specified, discover if the user put some prebuilt SDL3
# package in the 'extern' directory
if(NOT DEFINED SDL3_DIR)
	if(APPLE)
		set(_sdl3_maybe "${CMAKE_SOURCE_DIR}/extern/SDL3.framework/Resources/CMake")
	elseif(WIN32)
		set(_sdl3_maybe "${CMAKE_SOURCE_DIR}/extern/SDL3/cmake")
	endif()

	if(DEFINED _sdl3_maybe AND EXISTS "${_sdl3_maybe}")
		set(SDL3_DIR "${_sdl3_maybe}")
		message("Looking for SDL3 in: ${_sdl3_maybe}")
	endif()
	unset(_sdl3_maybe)
endif()

# Try to find system SDL3 first; fall back to building from source if not found
# and extern/SDL exists (required for Linux devcontainer and WASM/Emscripten)
if(NOT BUILD_SDL_FROM_SOURCE)
	find_package(SDL3 CONFIG QUIET)
	if(NOT SDL3_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/extern/SDL/CMakeLists.txt")
		set(BUILD_SDL_FROM_SOURCE ON CACHE BOOL "Build SDL from source (SDL3 not found, using extern/SDL)" FORCE)
		set(SDL3_DIR "${CMAKE_SOURCE_DIR}/extern/SDL")
		message("SDL3 not found; building from source: ${SDL3_DIR}")
	elseif(NOT SDL3_FOUND)
		find_package(SDL3 CONFIG REQUIRED)
	endif()
	if(SDL3_FOUND)
		message("Found pre-built SDL3")
	endif()
endif()

if(NOT BUILD_SDL_FROM_SOURCE)
	if(NOT SDL3_FOUND)
		find_package(SDL3 CONFIG REQUIRED)
	endif()
else()
	if(NOT DEFINED SDL3_DIR)
		set(SDL3_DIR "${CMAKE_SOURCE_DIR}/extern/SDL")
	endif()
	if(NOT EXISTS "${SDL3_DIR}/CMakeLists.txt")
		message(FATAL_ERROR "SDL3 source not found at ${SDL3_DIR}. "
			"For WASM builds, run: git clone https://github.com/libsdl-org/SDL extern/SDL")
	endif()
	# On Linux without X11/Wayland (e.g. headless devcontainer), skip video deps so SDL builds.
	# Pass -DSDL_SKIP_VIDEO_DEPS=OFF if you need native window support.
	option(SDL_SKIP_VIDEO_DEPS "Skip X11/Wayland requirement (for headless/WASM builds)" ON)
	if(UNIX AND NOT APPLE AND SDL_SKIP_VIDEO_DEPS)
		set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "Skip X11/Wayland" FORCE)
	endif()
	# Emscripten/WASM requires static SDL (shared libs not supported)
	if(EMSCRIPTEN)
		set(SDL_SHARED OFF CACHE BOOL "Emscripten: shared libs not supported" FORCE)
		set(SDL_STATIC ON CACHE BOOL "Emscripten: use static SDL" FORCE)
	endif()
	message("Building SDL3 from source: ${SDL3_DIR}")
	add_subdirectory("${SDL3_DIR}" EXCLUDE_FROM_ALL)
	# Make find_package(SDL3) succeed for Pomme when SDL is built from source
	set(SDL3_DIR "${CMAKE_SOURCE_DIR}/cmake")
endif()

#------------------------------------------------------------------------------
# DEPENDENCIES
#------------------------------------------------------------------------------

add_subdirectory(extern/Pomme)

find_package(OpenGL REQUIRED)

#------------------------------------------------------------------------------
# EXECUTABLE TARGET
#------------------------------------------------------------------------------

# Write header file containing version info
configure_file(${GAME_SRCDIR}/Headers/version.h.in ${GAME_SRCDIR}/Headers/version.h)

# Glob all source files
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${GAME_SRCDIR}/*.c ${GAME_SRCDIR}/*.cpp ${GAME_SRCDIR}/*.h)

# Emscripten: include gl_compat stub (glColorMaterial not in glemu). Exclude on native.
if(NOT EMSCRIPTEN)
	list(REMOVE_ITEM GAME_SOURCES "${GAME_SRCDIR}/QD3D/gl_compat_emscripten.c")
endif()

# Make source groups match file tree
source_group(TREE ${GAME_SRCDIR} PREFIX "" FILES ${GAME_SOURCES})

if(WIN32 AND NOT EMSCRIPTEN)
	# Windows resource file
	configure_file(${CMAKE_SOURCE_DIR}/packaging/win32.rc.in ${CMAKE_SOURCE_DIR}/packaging/${GAME_TARGET}.exe.rc)
	list(APPEND GAME_SOURCES ${CMAKE_SOURCE_DIR}/packaging/${GAME_TARGET}.exe.rc)
elseif(APPLE AND NOT EMSCRIPTEN)
	list(APPEND GAME_SOURCES ${CMAKE_SOURCE_DIR}/packaging/${GAME_TARGET}.icns)
	set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/${GAME_TARGET}.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

add_executable(${GAME_TARGET} ${GAME_SOURCES})

target_include_directories(${GAME_TARGET} PRIVATE ${GAME_SRCDIR}/Headers)

#------------------------------------------------------------------------------
# COMPILER/LINKER OPTIONS
#------------------------------------------------------------------------------

add_compile_definitions(
	"$<$<CONFIG:DEBUG>:_DEBUG>"
)

target_compile_definitions(${GAME_TARGET} PRIVATE
	GL_SILENCE_DEPRECATION)

if(NOT MSVC)
	target_compile_options(${GAME_TARGET} PRIVATE
		-fexceptions
		-Wall
		-Wextra
		-Wshadow
		-Werror=return-type
		$<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>
		-Wno-multichar
		-Wno-unknown-pragmas
		-Wstrict-aliasing=2
	)

	# Sanitizers in debug mode (Linux only)
	# When using a debugger, you should export LSAN_OPTIONS=detect_leaks=0
	if(SANITIZE)
		target_compile_options(${GAME_TARGET} PRIVATE
			-fsanitize=alignment
			-fsanitize=address
			-fsanitize=leak
			-fsanitize=undefined
			-fno-omit-frame-pointer
		)
	endif()
else()
	target_compile_definitions(${GAME_TARGET} PRIVATE
		WIN32_LEAN_AND_MEAN
		NOGDI				# don't clash with Mac function names
		NOUSER				# don't clash with Mac function names
	)

	target_compile_options(${GAME_TARGET} PRIVATE
		/EHs	# synchronous exceptions; also, extern "C" functions may throw exceptions
		/W4
		/wd4068 # ignore unrecognized pragmas
		/wd4100 # unreferenced formal parameters
		/wd4200 # nonstandard extension (zero-sized array in struct/union)
		/wd4201 # nonstandard extension (nameless struct)
		/wd4244 # conversion from double to float
		/wd4305 # truncation from double to float
		/wd5105 # see https://developercommunity.visualstudio.com/t/1249671
		/MP		# multiprocessor build
		/Zi		# output info to PDB
	)

	# Let executable be debugged with PDB, even in Release builds
	target_link_options(${GAME_TARGET} PRIVATE /DEBUG)

	# Enable console for debug builds
	set_target_properties(${GAME_TARGET} PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
	set_target_properties(${GAME_TARGET} PROPERTIES COMPILE_DEFINITIONS_DEBUG "_CONSOLE")
endif()

#------------------------------------------------------------------------------
# LINK LIBRARIES
#------------------------------------------------------------------------------

# Link sanitizers first
if(SANITIZE)
	target_link_libraries(${GAME_TARGET} PRIVATE asan ubsan)
endif()

# Explicitly link math lib on Linux (not on Emscripten; it's in the runtime)
if(NOT APPLE AND NOT WIN32 AND NOT EMSCRIPTEN)
	target_link_libraries(${GAME_TARGET} PRIVATE m)
endif()

# Emscripten: preload Data into virtual FS, WebGL2, HTML shell, legacy GL emulation
# Bugdom uses fixed-function OpenGL (glVertex3f, glLoadMatrixf, glLightfv, etc.)
# which WebGL 2 doesn't provide; LEGACY_GL_EMULATION emulates these on top of WebGL.
# Note: FULL_ES3 cannot be used with LEGACY_GL_EMULATION ("cannot emulate both ES2 and legacy GL").
#
# EMBED_DATA_FOR_WASM: Use --embed-file instead of --preload-file. Avoids "Unexpected error
# while handling" when the .data fetch fails (e.g. CursorBrowser, some proxies). Trade-off:
# Bugdom.js becomes ~70MB (Data inlined) vs separate Bugdom.data download.
option(EMBED_DATA_FOR_WASM "Embed Data in JS (avoids .data fetch; use if preload fails)" OFF)
if(EMSCRIPTEN)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sLEGACY_GL_EMULATION=1 -sGL_FFP_ONLY=1")
	if(EMBED_DATA_FOR_WASM)
		target_link_options(${GAME_TARGET} PRIVATE
			"SHELL:-s USE_WEBGL2=1"
			"SHELL:-s LEGACY_GL_EMULATION=1"
			"SHELL:-s GL_FFP_ONLY=1"
			"SHELL:-s GL_UNSAFE_OPTS=0"
			"SHELL:--pre-js ${CMAKE_SOURCE_DIR}/cmake/emscripten_gl_init_pre.js"
			"SHELL:--embed-file ${GAME_DATADIR}@/Data"
			"SHELL:-s ALLOW_MEMORY_GROWTH=1"
			"SHELL:-s ASSERTIONS=0"
			"SHELL:-s ASYNCIFY=1"
			"SHELL:-s ASYNCIFY_IMPORTS=emscripten_sleep"
			"SHELL:-s EXPORTED_RUNTIME_METHODS=HEAP8,HEAPU8,HEAP32,HEAPF32"
			"SHELL:-s EMULATE_FUNCTION_POINTER_CASTS=1"
			"SHELL:-o ${GAME_TARGET}.html"
		)
		message(STATUS "WASM: Embedding Data (no separate .data file)")
	else()
		target_link_options(${GAME_TARGET} PRIVATE
			"SHELL:-s USE_WEBGL2=1"
			"SHELL:-s LEGACY_GL_EMULATION=1"
			"SHELL:-s GL_FFP_ONLY=1"
			"SHELL:-s GL_UNSAFE_OPTS=0"
			"SHELL:--pre-js ${CMAKE_SOURCE_DIR}/cmake/emscripten_gl_init_pre.js"
			"SHELL:--preload-file ${GAME_DATADIR}@/Data"
			"SHELL:-s ALLOW_MEMORY_GROWTH=1"
			"SHELL:-s ASSERTIONS=0"
			"SHELL:-s ASYNCIFY=1"
			"SHELL:-s ASYNCIFY_IMPORTS=emscripten_sleep"
			"SHELL:-s EXPORTED_RUNTIME_METHODS=HEAP8,HEAPU8,HEAP32,HEAPF32"
			"SHELL:-s EMULATE_FUNCTION_POINTER_CASTS=1"
			"SHELL:-o ${GAME_TARGET}.html"
		)
	endif()
endif()

# Link SDL (Emscripten only builds static; native can use shared or static)
if(EMSCRIPTEN OR SDL_STATIC)
	target_link_libraries(${GAME_TARGET} PRIVATE SDL3::SDL3-static)
else()
	target_link_libraries(${GAME_TARGET} PRIVATE SDL3::SDL3)
endif()

# Link core dependencies
if(EMSCRIPTEN)
	# Use legacy GL emulation library - provides glVertex3f, glLoadMatrixf, glLightfv, etc.
	# that WebGL 2 doesn't have. Must match LEGACY_GL_EMULATION=1 in link flags above.
	# SDL references emscripten_webgl_get_proc_address, so we must use getprocaddr variant.
	target_link_libraries(${GAME_TARGET} PRIVATE Pomme "GL-emu-webgl2-getprocaddr")
else()
	target_link_libraries(${GAME_TARGET} PRIVATE Pomme OpenGL::GL)
endif()

# Add required frameworks for KillMacMouseAcceleration (desktop macOS only)
if(APPLE AND NOT EMSCRIPTEN)
	target_link_libraries(${GAME_TARGET} PRIVATE "-framework Foundation" "-framework IOKit")
endif()

#------------------------------------------------------------------------------
# PLATFORM-SPECIFIC PROPERTIES
#------------------------------------------------------------------------------

set_target_properties(${GAME_TARGET} PROPERTIES
	#--------------------------------------------------------------------------
	# EMSCRIPTEN (HTML shell generated via link option below)
	#--------------------------------------------------------------------------

	#--------------------------------------------------------------------------
	# MSVC/WIN32
	#--------------------------------------------------------------------------

	WIN32_EXECUTABLE					TRUE					# GUI application instead of console application
	VS_DEBUGGER_WORKING_DIRECTORY		"${CMAKE_SOURCE_DIR}"
	VS_DPI_AWARE						"PerMonitor"

	#--------------------------------------------------------------------------
	# APPLE
	#--------------------------------------------------------------------------

	# Build it as an .app bundle
	MACOSX_BUNDLE						TRUE

	# Set framework search path to (App bundle)/Contents/Frameworks so the game can use its embedded SDL3.framework
	XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/../Frameworks"

	# Explicitly turn off code signing, otherwise downloaded app will be quarantined forever
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""

	# Set up Info.plist values
	MACOSX_BUNDLE_ICON_FILE				"${GAME_TARGET}.icns"	# CFBundleIconFile
	MACOSX_BUNDLE_EXECUTABLE_NAME		${GAME_TARGET}			# CFBundleExecutable - executable name inside the bundle
	MACOSX_BUNDLE_SHORT_VERSION_STRING	${GAME_VERSION}			# CFBundleShortVersionString
	MACOSX_BUNDLE_COPYRIGHT				${GAME_COPYRIGHT}		# NSHumanReadableCopyright (supersedes CFBundleGetInfoString (MACOSX_BUNDLE_INFO_STRING))
	MACOSX_BUNDLE_BUNDLE_NAME			${GAME_FULL_NAME}		# CFBundleName - user-visible short name for the bundle, up to 15 characters
	MACOSX_BUNDLE_GUI_IDENTIFIER		${GAME_IDENTIFIER}		# CFBundleIdentifier - unique bundle ID in reverse-DNS format

	# If CODE_SIGN_IDENTITY is NOT empty: tell Xcode to codesign the app properly
	# Otherwise, if it's empty: explicitly turn off code signing, otherwise downloaded app will be quarantined forever
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${CODE_SIGN_IDENTITY}"
	XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY[variant=Debug] ""		# don't bother signing debug build

	# Bundle ID required for code signing - must match CFBundleIdentifier otherwise xcode will complain
	XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${GAME_IDENTIFIER}

	# Don't bother with universal builds when we're working on the debug version
	XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"

	XCODE_EMBED_FRAMEWORKS							"extern/SDL3.framework"
	XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY		"YES"		# frameworks must be signed by the same developer as the binary
	XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY	"YES"		# not strictly necessary, but that's cleaner
	XCODE_ATTRIBUTE_COPY_PHASE_STRIP[variant=Debug]	"NO"		# avoid "skipping copy phase strip" warning while working on Debug config

	# The following is to pass notarization requirements
	XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME			"YES"
	XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS "NO"
	XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS			"--options=runtime --timestamp"
)

#------------------------------------------------------------------------------
# POST-BUILD (COPY ASSETS, ETC.)
#------------------------------------------------------------------------------

if(APPLE AND NOT EMSCRIPTEN)
	set(GAME_DATA_TARGET_LOCATION "$<TARGET_FILE_DIR:${GAME_TARGET}>/../Resources")
elseif(NOT EMSCRIPTEN)
	set(GAME_DATA_TARGET_LOCATION "$<TARGET_FILE_DIR:${GAME_TARGET}>/Data")
endif()

if(NOT EMSCRIPTEN)
	add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${GAME_DATADIR}" "${GAME_DATA_TARGET_LOCATION}")
else()
	# Emscripten may not produce Bugdom.html from -o flag; ensure we have a loader
	add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_SOURCE_DIR}/packaging/bugdom-wasm.html"
			"$<TARGET_FILE_DIR:${GAME_TARGET}>/Bugdom.html")
	# Workaround: Emscripten legacy GL emulation bugs when using emscripten_webgl_* (SDL path).
	# 1. getCurTexUnit returns null before TexEnvJIT.init() runs.
	# 2. newRenderingFrameStarted crashes when tempVertexBufferCounters1 is uninitialized.
	# 3. moduleContextCreatedCallbacks never run (GLImmediate.init, generateTempBuffers).
	add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C
			sh "${CMAKE_SOURCE_DIR}/cmake/patch_emscripten_gl.sh"
			"$<TARGET_FILE_DIR:${GAME_TARGET}>/Bugdom.js"
		COMMENT "Patch Emscripten legacy GL emulation bugs")
endif()

if(APPLE)
	set(INFO_PLIST "${GAME_DATA_TARGET_LOCATION}/../Info.plist")

	add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
		# Copy extra resources to app bundle
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/packaging/mainmenu.nib ${GAME_DATA_TARGET_LOCATION}/mainmenu.nib

		# Info.plist: high-DPI support, Game Mode, honor mainmenu.nib
		COMMAND plutil -replace NSHighResolutionCapable -bool true ${INFO_PLIST}
		COMMAND plutil -replace LSApplicationCategoryType -string "public.app-category.games" ${INFO_PLIST}
		COMMAND plutil -replace NSMainNibFile -string "mainmenu" ${INFO_PLIST}
	)
endif()

# Windows-specific libraries
if(WIN32)
	# Copy SDL3 DLLs to output folder on Windows for convenience
	add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${GAME_TARGET}> $<TARGET_FILE_DIR:${GAME_TARGET}>)

	# When installing (cmake --install), copy Visual Studio redistributable DLLs to install location
	include(InstallRequiredSystemLibraries)
endif()

# Copy documentation to output folder
configure_file(${CMAKE_SOURCE_DIR}/packaging/ReadMe.txt.in ${CMAKE_CURRENT_BINARY_DIR}/ReadMe.txt)
