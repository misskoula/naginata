cmake_minimum_required(VERSION 3.25.0)

# Path to the Steinberg VST3 SDK checkout (base, pluginterfaces, public.sdk, cmake submodules).
set(VST3_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../vst3sdk" CACHE PATH "VST3 SDK root")

# Naginata needs no GUI yet and no SDK examples: keep the build small.
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)

list(APPEND CMAKE_MODULE_PATH "${VST3_SDK}/cmake/modules")
include(SMTG_VST3_SDK)

project(Naginata VERSION 0.1.0 DESCRIPTION "Naginata: fuzz into wavefolder")

# VSTGUI static libraries are linked into the plugin shared module, so they need -fPIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# moduleinfo.json needs the host-side moduleinfotool, which a cross-build can't run. Windows
# bundles don't need it. Turn it off for cross-builds only.
if(CMAKE_CROSSCOMPILING)
    set(SMTG_CREATE_MODULE_INFO OFF)
endif()

set(SDK_ROOT "${VST3_SDK}")
set(public_sdk_SOURCE_DIR ${SDK_ROOT}/public.sdk)
set(pluginterfaces_SOURCE_DIR ${SDK_ROOT}/pluginterfaces)
include_directories(${SDK_ROOT})

# VSTGUI: the editor is drawn procedurally in source/naginata_editor.cpp.
smtg_enable_vstgui_support(VSTGUI_SOURCE_DIR "${SDK_ROOT}/vstgui4")

smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()

# SDK base libraries needed by SingleComponentEffect and the plugin factory.
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_target()

# MinGW only: the SDK's data-exchange code calls std::aligned_alloc, which MSVCRT lacks. See mingw_shim.h.
if(MINGW)
    target_compile_options(sdk PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/mingw_shim.h)
endif()
# Host-side tools (validator, moduleinfotool) only build natively. Cross-builds skip them.
if(NOT CMAKE_CROSSCOMPILING)
    smtg_create_public_sdk_hosting_target()  # needed by moduleinfotool and the validator

    # Trial expiry (unix seconds) is passed in by the build script; 0 disables the lock.
    set(NAGINATA_TRIAL_EXPIRY "0" CACHE STRING "Unix time after which output is muted")

    # Steinberg validator, used to check the built bundle against the VST3 spec.
    add_subdirectory(${SDK_ROOT}/public.sdk/samples/vst-hosting/validator
                     ${CMAKE_CURRENT_BINARY_DIR}/validator)

    # moduleinfotool generates the moduleinfo.json each VST3 bundle needs (post-build step).
    add_subdirectory(${SDK_ROOT}/public.sdk/samples/vst-utilities/moduleinfotool
                     ${CMAKE_CURRENT_BINARY_DIR}/moduleinfotool)
endif()
set(NAGINATA_TRIAL_EXPIRY "0" CACHE STRING "Unix time after which output is muted")

set(naginata_sources
    ${SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/naginata_vst3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/naginata_vst3.h
    ${CMAKE_CURRENT_SOURCE_DIR}/source/naginata_editor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/naginata_editor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/source/naginata_presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/naginata_presets.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/naginata_dsp.h
)

smtg_add_vst3plugin(Naginata ${naginata_sources})

if(CMAKE_CROSSCOMPILING AND MINGW)
    # Only the Linux-to-Windows cross-build needs this: the SDK adds linuxmain.cpp there, so the
    # Windows InitDll/ExitDll are supplied here. Native MSVC builds get them from the SDK's dllmain.
    target_sources(Naginata PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source/win_entry.cpp)
    target_link_options(Naginata PRIVATE -static-libstdc++ -static-libgcc)
endif()

target_compile_features(Naginata PUBLIC cxx_std_17)
target_compile_definitions(Naginata PRIVATE NAGINATA_TRIAL_EXPIRY=${NAGINATA_TRIAL_EXPIRY}LL)
target_include_directories(Naginata PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Naginata PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(Naginata
    RESOURCES
        resource/ngnt.uidesc
)
