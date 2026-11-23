# Builds the Facet design framework (third_party/facet) and the libraries it needs as static
# libraries for steam-account-manager.vcxproj, which compiles Dear ImGui itself.
#
# Run through scripts/build_facet.ps1; the vcxproj calls that script before it compiles. The
# script passes SAM_LIB_DIR, and every library lands in SAM_LIB_DIR/<Config>/:
#   facet.lib, lunasvg.lib, plutovg.lib, freetype.lib (and libprotobufd.lib in Debug, see below)
#
# Facet is compiled against SAM_IMGUI, an INTERFACE target that carries the include
# directories and the compile definitions the exe compiles Dear ImGui with. They must stay
# identical to the ImGui definitions in steam-account-manager.vcxproj: Facet's objects and the
# exe's ImGui objects share every ImGui struct, so a definition that changes a layout
# (IMGUI_USE_WCHAR32 changes ImWchar, IMGUI_ENABLE_FREETYPE the font loader) must be on both
# sides or on neither.
cmake_minimum_required(VERSION 3.28)
project(SamFacet LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "This wrapper builds the MSVC libraries of steam-account-manager only.")
endif()

get_filename_component(SAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SAM_THIRD_PARTY "${SAM_ROOT}/third_party")
set(SAM_LIB_DIR "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Where the static libraries are written")

foreach(required imgui/imgui.h facet/CMakeLists.txt freetype/CMakeLists.txt)
    if(NOT EXISTS "${SAM_THIRD_PARTY}/${required}")
        message(FATAL_ERROR "third_party/${required} is missing; run scripts/init_third_party.ps1")
    endif()
endforeach()

# The exe links the static runtime (/MTd in Debug, /MT in Release), so every library here has
# to as well, the dependencies Facet fetches included. Their own cmake_minimum_required may
# predate policy CMP0091, so its default is set too.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
# option() honours the normal variables set below even in projects that predate CMP0077.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# One flat folder per configuration, the same file names in Debug and Release.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${SAM_LIB_DIR}")
set(CMAKE_DEBUG_POSTFIX "")
set(BUILD_SHARED_LIBS OFF)

# --- FreeType, with the options of Facet's cmake/FacetDeps.cmake ------------------------------
set(FT_DISABLE_ZLIB ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_BROTLI ON)
set(SKIP_INSTALL_ALL ON)
set(DISABLE_FORCE_DEBUG_POSTFIX ON)
add_subdirectory("${SAM_THIRD_PARTY}/freetype" freetype EXCLUDE_FROM_ALL SYSTEM)

# --- Dear ImGui as the exe compiles it --------------------------------------------------------
add_library(sam_imgui INTERFACE)
target_include_directories(sam_imgui SYSTEM INTERFACE
    "${SAM_THIRD_PARTY}/imgui"
    "${SAM_THIRD_PARTY}/imgui/backends"
    "${SAM_THIRD_PARTY}/imgui/misc/freetype")
target_compile_definitions(sam_imgui INTERFACE
    IMGUI_DEFINE_MATH_OPERATORS
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_DISABLE_OBSOLETE_KEYIO
    IMGUI_USE_WCHAR32
    IMGUI_ENABLE_FREETYPE)
target_compile_features(sam_imgui INTERFACE cxx_std_20)

# --- Facet --------------------------------------------------------------------------------------
set(FACET_IMGUI_TARGET sam_imgui)
set(FACET_BUILD_DEMO OFF)
set(FACET_BUILD_TESTS OFF)
add_subdirectory("${SAM_THIRD_PARTY}/facet" facet)

foreach(lib facet lunasvg plutovg freetype)
    if(NOT TARGET ${lib})
        message(FATAL_ERROR "expected target '${lib}' was not created")
    endif()
    set_target_properties(${lib} PROPERTIES DEBUG_POSTFIX "" ARCHIVE_OUTPUT_DIRECTORY "${SAM_LIB_DIR}")
endforeach()

# One target that builds every library the exe links.
add_custom_target(sam_facet_libs ALL)
add_dependencies(sam_facet_libs facet lunasvg plutovg freetype)

# --- libprotobuf for Debug builds -----------------------------------------------------------
# The tracked third_party/protobuf/lib/libprotobuf.lib is a Release /MT build, which a /MTd
# exe cannot link. The same release (third_party/protobuf-src, fetched by
# init_third_party.ps1) is built here as SAM_LIB_DIR/Debug/libprotobufd.lib. Only
# build_facet.ps1 -Configuration Debug builds this target; Release keeps the tracked library.
set(SAM_PROTOBUF_SRC "${SAM_THIRD_PARTY}/protobuf-src")
if(EXISTS "${SAM_PROTOBUF_SRC}/CMakeLists.txt")
    set(protobuf_BUILD_TESTS OFF)
    set(protobuf_BUILD_PROTOC_BINARIES OFF)
    set(protobuf_BUILD_LIBPROTOC OFF)
    set(protobuf_WITH_ZLIB OFF)
    set(protobuf_INSTALL OFF)
    set(protobuf_MSVC_STATIC_RUNTIME ON)
    add_subdirectory("${SAM_PROTOBUF_SRC}" protobuf EXCLUDE_FROM_ALL SYSTEM)
    set_target_properties(libprotobuf PROPERTIES DEBUG_POSTFIX "d" ARCHIVE_OUTPUT_DIRECTORY "${SAM_LIB_DIR}")
    add_custom_target(sam_protobuf_debug)
    add_dependencies(sam_protobuf_debug libprotobuf)
else()
    message(WARNING "third_party/protobuf-src is missing; Debug builds cannot link. "
                    "Run scripts/init_third_party.ps1.")
endif()
