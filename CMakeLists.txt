cmake_minimum_required(VERSION 3.14)
project(llm)

# 设置 C++ 标准
# set(CMAKE_CXX_STANDARD 11)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS Off) # 可选，但推荐关闭编译器特定扩展


# 设置构建类型（Debug/Release）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()


# 添加头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 设置 lib 目录路径
set(LIB_DIR ${PROJECT_SOURCE_DIR}/lib)

# 查找 lib 目录下的所有 .so 文件（Linux） 和 .a 文件（静态库）
file(GLOB LIBS "${LIB_DIR}/*.so" "${LIB_DIR}/*.a")


if(LIBS)
    message(STATUS "Found shared libraries: ${LIBS}")
else()
    message(FATAL_ERROR "No .so files found in ${LIB_DIR}, please make sure they exist.")
endif()

# 添加可执行文件
add_executable(${PROJECT_NAME} 
src/main.cpp
src/encapsule.cpp
)


# 链接所有 .so 文件
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${LIBS}
        pthread
        m
)

# 设置运行时库路径（RPATH），让程序运行时能找到 .so 文件
set(CMAKE_INSTALL_RPATH "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)