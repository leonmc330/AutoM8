cmake_minimum_required(VERSION 3.16)
project(AutoM8 VERSION 0.1.0 LANGUAGES C CXX)

# Linux:   system SDL2 + PCRE2 by default (apt install libsdl2-dev libpcre2-dev).
# Windows: SDL2 + PCRE2 are downloaded and linked statically, so the .exe files run alone.
if(WIN32)
	set(AUTOM8_VENDORED_DEFAULT ON)
else()
	set(AUTOM8_VENDORED_DEFAULT OFF)
endif()
option(AUTOM8_VENDORED_DEPS "Download SDL2 and PCRE2 and link them statically" ${AUTOM8_VENDORED_DEFAULT})
option(AUTOM8_GUI "Build the two programs (needs SDL2); OFF = only the engine and its tests" ON)
option(AUTOM8_TESTS "Build the unit tests" ON)
option(AUTOM8_SANITIZE "Build with AddressSanitizer + UBSan (GCC / Clang)" OFF)
# Release number set by CI (1 -> "v001"); 0 = a local build, shown as "dev"
set(AUTOM8_BUILD_NUMBER 0 CACHE STRING "Release number, 1-999 (0 = dev build)")
if(AUTOM8_BUILD_NUMBER GREATER 0)
	math(EXPR padded "${AUTOM8_BUILD_NUMBER} + 1000") # 5 -> 1005 -> "005"
	string(SUBSTRING "${padded}" 1 3 padded)
	set(AUTOM8_VERSION_LABEL "v${padded}")
else()
	set(AUTOM8_VERSION_LABEL "dev")
endif()
message(STATUS "AutoM8 version: ${AUTOM8_VERSION_LABEL}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
# MSVC: static C/C++ runtime, so users don't need the Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(AUTOM8_SANITIZE)
	add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address,undefined)
endif()

# ------------------------------------------------------------ dependencies ---

if(AUTOM8_VENDORED_DEPS)
	include(FetchContent)
	if(POLICY CMP0135)
		cmake_policy(SET CMP0135 NEW) # extracted files get the extraction time: clean rebuilds on URL change
	endif()
	set(exclude "")
	if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
		set(exclude EXCLUDE_FROM_ALL) # keep their install rules out of ours
	endif()

	if(AUTOM8_GUI)
		set(SDL_SHARED OFF CACHE BOOL "" FORCE)
		set(SDL_STATIC ON CACHE BOOL "" FORCE)
		set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
		set(SDL_TEST OFF CACHE BOOL "" FORCE)
		set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
		set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
		set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
		FetchContent_Declare(SDL2
			URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
			URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
			${exclude})
		FetchContent_MakeAvailable(SDL2)
		set(SDL2_TARGET SDL2::SDL2-static)
	endif()

	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
	set(PCRE2_STATIC_PIC ON CACHE BOOL "" FORCE)
	set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
	set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(pcre2
		URL https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.47/pcre2-10.47.tar.bz2
		URL_HASH SHA256=47fe8c99461250d42f89e6e8fdaeba9da057855d06eb7fc08d9ca03fd08d7bc7
		${exclude})

	FetchContent_MakeAvailable(pcre2)
	set(PCRE2_TARGET pcre2-8-static)
else()
	if(AUTOM8_GUI)
		find_package(SDL2 REQUIRED CONFIG)
		set(SDL2_TARGET SDL2::SDL2)
	endif()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)
	set(PCRE2_TARGET PkgConfig::PCRE2)
endif()

# Dear ImGui (vendored), drawn with SDL_Renderer
set(IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)
if(AUTOM8_GUI)
	add_library(imgui STATIC
		${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
		${IMGUI}/misc/cpp/imgui_stdlib.cpp
		${IMGUI}/backends/imgui_impl_sdl2.cpp ${IMGUI}/backends/imgui_impl_sdlrenderer2.cpp)
	target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends)
	target_link_libraries(imgui PUBLIC ${SDL2_TARGET})
endif()

# ------------------------------------------------------------- our code ---

function(autom8_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
	endif()
	if(WIN32)
		target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
	endif()
endfunction()

# document model, JSON, helpers
add_library(autom8_common STATIC src/common/blocks.cpp src/common/config.cpp src/common/util.cpp)
target_include_directories(autom8_common PUBLIC src/common third_party)
if(WIN32)
	target_link_libraries(autom8_common PUBLIC shell32)
endif()
autom8_warnings(autom8_common)

# sequence engine, processes, regex
if(WIN32)
	set(PROCESS_OS_SRC src/runner/process_win.cpp)
else()
	set(PROCESS_OS_SRC src/runner/process_posix.cpp)
endif()
add_library(autom8_engine STATIC
	src/runner/process.cpp ${PROCESS_OS_SRC} src/runner/regex.cpp src/runner/runner.cpp)
target_include_directories(autom8_engine PUBLIC src/runner)
target_link_libraries(autom8_engine PUBLIC autom8_common PRIVATE ${PCRE2_TARGET})
autom8_warnings(autom8_engine)

# block moves (editor logic that needs no window)
add_library(autom8_moves STATIC src/editor/moves.cpp)
target_include_directories(autom8_moves PUBLIC src/editor)
target_link_libraries(autom8_moves PUBLIC autom8_common)
autom8_warnings(autom8_moves)


if(WIN32)
	string(REPLACE "." "," AUTOM8_VERSION_RC "${PROJECT_VERSION}.${AUTOM8_BUILD_NUMBER}")
	set(AUTOM8_MANIFEST ${CMAKE_SOURCE_DIR}/res/autom8.manifest)
	configure_file(res/windows.rc.in ${CMAKE_BINARY_DIR}/windows.rc @ONLY)
	set(WIN_RES ${CMAKE_BINARY_DIR}/windows.rc)
endif()

function(autom8_program target)
	autom8_warnings(${target})
	target_compile_definitions(${target} PRIVATE AUTOM8_VERSION="${AUTOM8_VERSION_LABEL}")
	if(WIN32)
		target_sources(${target} PRIVATE ${WIN_RES})
		if(MSVC)
			target_link_options(${target} PRIVATE /MANIFEST:NO) # ours comes from windows.rc
		endif()
		if(MINGW)
			target_link_options(${target} PRIVATE -static) # no libc++ / winpthread DLLs next to the .exe
		endif()
	elseif(AUTOM8_VENDORED_DEPS AND NOT APPLE)
		# release builds: only depend on glibc, so one binary runs on most distros
		target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
	endif()
endfunction()

if(AUTOM8_GUI)
	# window + widgets shared by both programs
	add_library(autom8_gui STATIC src/common/gui.cpp)
	target_link_libraries(autom8_gui PUBLIC autom8_common imgui)
	autom8_warnings(autom8_gui)

	add_executable(autom8 src/runner/main.cpp src/runner/window.cpp)
	target_link_libraries(autom8 PRIVATE autom8_engine autom8_gui)
	autom8_program(autom8)

	find_package(Threads REQUIRED) # the file dialog runs on a thread
	add_executable(autom8-editor WIN32 src/editor/main.cpp src/editor/editor.cpp src/editor/file_dialog.cpp) # WIN32: no console window
	target_link_libraries(autom8-editor PRIVATE autom8_moves autom8_gui Threads::Threads)
	if(WIN32)
		target_link_libraries(autom8-editor PRIVATE comdlg32 ole32)
	endif()
	autom8_program(autom8-editor)

	# THIRD_PARTY_LICENSES.txt next to the programs: the notices binary copies must carry
	set(license_files
		"Dear ImGui|${IMGUI}/LICENSE.txt"
		"nlohmann/json|${CMAKE_SOURCE_DIR}/third_party/nlohmann/LICENSE.MIT")
	if(AUTOM8_VENDORED_DEPS)
		list(APPEND license_files "SDL2|${sdl2_SOURCE_DIR}/LICENSE.txt" "PCRE2|${pcre2_SOURCE_DIR}/LICENCE.md")
	endif()
	set(licenses "AutoM8 includes the following third-party software.\n")
	foreach(entry ${license_files})
		string(REPLACE "|" ";" entry "${entry}")
		list(GET entry 0 lib_name)
		list(GET entry 1 lib_file)
		file(READ "${lib_file}" text)
		string(APPEND licenses "\n==================================== ${lib_name} ====================================\n\n${text}")
	endforeach()
	file(WRITE ${CMAKE_BINARY_DIR}/THIRD_PARTY_LICENSES.txt "${licenses}")

	install(TARGETS autom8 autom8-editor RUNTIME DESTINATION bin)
endif()

if(AUTOM8_TESTS)
	enable_testing()
	add_executable(autom8-tests tests/tests.cpp)
	target_link_libraries(autom8-tests PRIVATE autom8_engine autom8_moves)
	autom8_program(autom8-tests)
	add_test(NAME unit COMMAND autom8-tests)
endif()
