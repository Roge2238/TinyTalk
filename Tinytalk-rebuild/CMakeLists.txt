cmake_minimum_required(VERSION 3.10)
project(Tinytalk-rebuild VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 阶段说明:构建门禁(M0)。当前 4 个模块已能编译链接;
# 拆目录(net/app/game)后这里的文件清单会跟着拆。
add_executable(tinytalkd
    Mainserver.cpp
    server.cpp
    game_server.cpp
    timeout_handle.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(tinytalkd Threads::Threads)

# 开发期告警全开;以后调内存问题可以加: -fsanitize=address
target_compile_options(tinytalkd PRIVATE -Wall -Wextra)
