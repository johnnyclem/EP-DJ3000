# Wrapper that compiles the portable firmware core in place (no copy): firmware/core/src/*.cpp, headers from
# firmware/core/include. The core is warning-clean under -Wconversion/-Werror, and so stays.
set(core_dir "${CMAKE_CURRENT_LIST_DIR}/../../../core")
get_filename_component(core_dir "${core_dir}" ABSOLUTE)

# The first pass over component CMakeLists only collects requirements (script mode, where CONFIGURE_DEPENDS is
# invalid); the globbed list is needed from the real pass on. CONFIGURE_DEPENDS re-runs CMake when a core
# source file is added or removed.
if(CMAKE_BUILD_EARLY_EXPANSION)
    set(core_srcs)
else()
    file(GLOB core_srcs CONFIGURE_DEPENDS "${core_dir}/src/*.cpp")
endif()

idf_component_register(SRCS ${core_srcs}
                       INCLUDE_DIRS "${core_dir}/include")

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Werror
    -fno-exceptions -fno-rtti)
