cmake_minimum_required(VERSION 3.19)
project(AutoRun_Codex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_standard_project_setup()

set(APP_SOURCES
    main.cpp
    core/CodexPaths.cpp
    core/CodexWriterLock.cpp
    core/JsonRpcProcess.cpp
    core/CodexClient.cpp
    core/AccountProfile.cpp
    core/RolloutScanner.cpp
    core/ThreadCatalog.cpp
    core/JobEngine.cpp
    core/SettingsStore.cpp
    core/WindowsAcrylic.cpp
    ui/RingWidget.cpp
    ui/PillProgress.cpp
    ui/Theme.cpp
    ui/WatchBadge.cpp
    ui/DualRingWidget.cpp
    ui/QuotaCard.cpp
    ui/QuotaWidget.cpp
    ui/ThreadTableModel.cpp
    ui/ThreadSettingsDialog.cpp
    ui/SettingsDialog.cpp
    ui/MainWindow.cpp
    ui/AvatarUtils.cpp
)

set(APP_HEADERS
    core/Types.h
    core/CodexPaths.h
    core/CodexWriterLock.h
    core/SettingsStore.h
    core/WindowsAcrylic.h
    core/JsonRpcProcess.h
    core/CodexClient.h
    core/AccountProfile.h
    core/RolloutScanner.h
    core/ThreadCatalog.h
    core/JobEngine.h
    ui/RingWidget.h
    ui/PillProgress.h
    ui/Theme.h
    ui/WatchBadge.h
    ui/DualRingWidget.h
    ui/QuotaCard.h
    ui/QuotaWidget.h
    ui/ThreadTableModel.h
    ui/ThreadSettingsDialog.h
    ui/SettingsDialog.h
    ui/MainWindow.h
    ui/AvatarUtils.h
)

qt_add_executable(AutoRun_Codex
    WIN32
    MANUAL_FINALIZATION
    ${APP_SOURCES}
    ${APP_HEADERS}
)

target_include_directories(AutoRun_Codex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(AutoRun_Codex PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

qt_add_resources(AutoRun_Codex "app_resources"
    PREFIX "/resources"
    BASE "resources"
    FILES
        resources/openai-logo-white.png
        resources/HarmonyOS_SansSC_TW.ttf
)

qt_generate_deploy_app_script(
    TARGET AutoRun_Codex
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(TARGETS AutoRun_Codex BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(SCRIPT ${deploy_script})

qt_finalize_executable(AutoRun_Codex)