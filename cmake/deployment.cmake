# Packaging
# Qv2ray Development and Research WorkGroup
set(CPACK_PACKAGE_VENDOR "PolyCore Development Group")
set(CPACK_PACKAGE_VERSION ${QV2RAY_VERSION_STRING})
set(CPACK_PACKAGE_DESCRIPTION "Cross-platform V2Ray Client written in Qt.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://polycore.example")
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/assets/icons/qv2ray.ico")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    if(BUILD_NSIS)
        add_definitions(-DQV2RAY_NO_ASIDECONFIG)
        set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/assets/icons\\\\qv2ray.ico")
        set(CPACK_GENERATOR "NSIS")
        set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/icons/qv2ray.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/assets/icons/qv2ray.ico")
        set(CPACK_NSIS_DISPLAY_NAME "PolyCore")
        set(CPACK_NSIS_PACKAGE_NAME "PolyCore")
        set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "
            ExecWait \"taskkill /f /im polycore.exe\"
            ExecWait \"taskkill /f /im v2ray.exe\"
            ExecWait \"taskkill /f /im wv2ray.exe\"
            ExecWait \"taskkill /f /im xray.exe\"
            ")
        set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
            CreateShortCut \"$DESKTOP\\PolyCore.lnk\" \"$INSTDIR\\polycore.exe\"
            CreateDirectory \"$SMPROGRAMS\\$STARTMENU_FOLDER\\PolyCore\"
            CreateShortCut \"$SMPROGRAMS\\$STARTMENU_FOLDER\\PolyCore\\PolyCore.lnk\" \"$INSTDIR\\polycore.exe\"
            WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\polycore\" \"DisplayIcon\" \"$INSTDIR\\polycore.exe\"
            WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\polycore\" \"HelpLink\" \"https://polycore.example\"
            WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\polycore\" \"InstallLocation\" \"$INSTDIR\"
            WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\polycore\" \"URLUpdateInfo\" \"https://github.com/polycore/polycore/releases\"
            WriteRegStr HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\polycore\" \"URLInfoAbout\" \"https://github.com/polycore/polycore\"
            ")
        set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
            ExecWait \"taskkill /f /im polycore.exe\"
            Delete \"$DESKTOP\\PolyCore.lnk\"
            Delete \"$SMPROGRAMS\\$STARTMENU_FOLDER\\PolyCore\\PolyCore.lnk\"
            RMDir \"$SMPROGRAMS\\$STARTMENU_FOLDER\\PolyCore\"
            DeleteRegKey HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\polycore\"
            ")
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "polycore")
    endif()
endif()

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    if(DS_STORE_SCRIPT)
        set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/CMakeDMGSetup.scpt")
    else()
        set(CPACK_DMG_DS_STORE "${CMAKE_SOURCE_DIR}/assets/DS_Store")
    endif()
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_SOURCE_DIR}/assets/CMakeDMGBackground.png")
    configure_file("${CMAKE_SOURCE_DIR}/assets/package_dmg.json.in" "${CMAKE_SOURCE_DIR}/assets/package_dmg.json" @ONLY)
endif()

include(CPack)

# Directories to look for dependencies
set(DIRS "${CMAKE_BINARY_DIR}")

# Path used for searching by FIND_XXX(), with appropriate suffixes added
if(CMAKE_PREFIX_PATH)
    foreach(dir ${CMAKE_PREFIX_PATH})
        list(APPEND DIRS "${dir}/bin" "${dir}/lib")
    endforeach()
endif()

# Append Qt's lib folder which is two levels above Qt5Widgets_DIR
if(QV2RAY_QT6)
    list(APPEND DIRS "${Qt6Core_DIR}/../..")
else()
    list(APPEND DIRS "${Qt5Core_DIR}/../..")
endif()

list(APPEND DIRS "/usr/local/lib")
list(APPEND DIRS "/usr/lib")

include(InstallRequiredSystemLibraries)

message(STATUS "APPS: ${APPS}")
message(STATUS "QT_PLUGINS: ${QT_PLUGINS}")
message(STATUS "DIRS: ${DIRS}")

install(CODE "
    include(BundleUtilities)
    fixup_bundle(\"${APPS}\"   \"\"   \"${DIRS}\")
    " COMPONENT Runtime)
