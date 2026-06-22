cmake_minimum_required (VERSION 3.29)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project (EngineVulkan)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(Vulkan 1.4.335 REQUIRED)
find_package(tinyobjloader REQUIRED)
find_package(tinygltf REQUIRED)
find_package(KTX REQUIRED)

add_library(VulkanCppModule)
add_library(Vulkan::cppm ALIAS VulkanCppModule)

target_compile_definitions(VulkanCppModule
  PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
target_include_directories(VulkanCppModule
  PUBLIC "${Vulkan_INCLUDE_DIR}"
)
target_link_libraries(VulkanCppModule
  PUBLIC Vulkan::Vulkan
)

set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD 23)

target_sources(VulkanCppModule
  PUBLIC
  FILE_SET cxx_modules TYPE CXX_MODULES
  BASE_DIRS "${Vulkan_INCLUDE_DIR}"
  FILES "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)

target_sources(VulkanCppModule
  PRIVATE "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)

find_package(stb REQUIRED)
set(STB_INCLUDEDIR ${stb_INCLUDE_DIRS})

add_executable (glslang::validator IMPORTED)
find_program (GLSLANG_VALIDATOR "glslangValidator" HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
set_property (TARGET glslang::validator PROPERTY IMPORTED_LOCATION "${GLSLANG_VALIDATOR}")
find_program (SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

# Define the source and destination shader directories
set(SHADERS_SRC_DIR "${PROJECT_SOURCE_DIR}/shaders")
set(SHADERS_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

# Create the output directory in the build folder if it doesn't exist
file(MAKE_DIRECTORY "${SHADERS_GEN_DIR}")

# This list will collect every generated .spv file path
set(ALL_GENERATED_SPV_FILES "")

file(GLOB_RECURSE SLANG_SOURCE_FILES
    "${SHADERS_SRC_DIR}/*.slang"
)

foreach(SLANG_FILE ${SLANG_SOURCE_FILES})
    get_filename_component(FILE_NAME ${SLANG_FILE} NAME)
    get_filename_component(NAME_WE ${SLANG_FILE} NAME_WE)
    
    set(OUTPUT_SPV "${SHADERS_GEN_DIR}/${NAME_WE}_slang.spv")
    list(APPEND ALL_GENERATED_SPV_FILES "${OUTPUT_SPV}")

    # Start with the standard 3D rendering entry points
    set(ENTRY_POINTS -entry vertMain -entry fragMain)
    
    # Read the text inside the .slang file to see if it contains a compute shader
    file(READ "${SLANG_FILE}" SLANG_CONTENTS)
    if(SLANG_CONTENTS MATCHES "compMain")
        # If it has compMain, safely append it to the compiler target switches!
        list(APPEND ENTRY_POINTS -entry compMain)
    endif()

    add_custom_command(
        OUTPUT "${OUTPUT_SPV}"
        COMMAND "${SLANGC_EXECUTABLE}"
        ARGS "${SLANG_FILE}" 
             -target spirv 
             -profile spirv_1_4+spvRayQueryKHR 
             -emit-spirv-directly 
             -fvk-use-entrypoint-name 
             ${ENTRY_POINTS} 
             -o "${OUTPUT_SPV}"
        DEPENDS "${SLANG_FILE}"
        COMMENT "Compiling Slang Shader: ${FILE_NAME}"
        VERBATIM
    )
endforeach()

add_custom_target(compile_shaders DEPENDS ${ALL_GENERATED_SPV_FILES})

# Gather all C++ source files (.cpp)
file(GLOB_RECURSE SOURCE_FILES 
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

# Create the main game executable target using the project name (EngineVulkan)
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

file(GLOB MODULE_FILES "${PROJECT_SOURCE_DIR}/src/*.cppm")

target_sources(${PROJECT_NAME} PUBLIC
 FILE_SET CXX_MODULES FILES
 ${MODULE_FILES}
)

# Add header files
target_include_directories(${PROJECT_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/include")

# Add all libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::cppm          # The Vulkan C++ module 
    glfw                  # GLFW windowing toolkit
    glm::glm              # OpenGL Mathematics library
    tinyobjloader         # OBJ 3D model loader
    tinygltf::tinygltf    # GLTF 3D model loader
    KTX::ktx              # KTX texture loader
)

add_dependencies(${PROJECT_NAME} compile_shaders)
target_compile_definitions(${PROJECT_NAME} PRIVATE "VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS" )


