cmake_minimum_required(VERSION 3.21)
project(NastiDesigner VERSION 0.3 LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
	add_compile_options(/utf-8)
endif()

set(BUNDLED_OPENCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.native-deps/opencv/lib/cmake/opencv4")
if(NOT OpenCV_DIR AND EXISTS "${BUNDLED_OPENCV_DIR}/OpenCVConfig.cmake")
	set(OpenCV_DIR "${BUNDLED_OPENCV_DIR}" CACHE PATH "OpenCV package directory" FORCE)
endif()
if(NOT Qt6_DIR AND NOT CMAKE_PREFIX_PATH
	AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.native-deps/qt/lib/cmake/Qt6/Qt6Config.cmake")
	list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.native-deps/qt")
	if(APPLE AND NOT CMAKE_OSX_SYSROOT)
		file(GLOB COMPATIBLE_MACOS_SDKS
			"/Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk")
		list(SORT COMPATIBLE_MACOS_SDKS COMPARE NATURAL ORDER DESCENDING)
		foreach(SDK_PATH IN LISTS COMPATIBLE_MACOS_SDKS)
			if(EXISTS "${SDK_PATH}/System/Library/Frameworks/AGL.framework/AGL.tbd")
				set(CMAKE_OSX_SYSROOT "${SDK_PATH}" CACHE PATH
					"macOS SDK compatible with bundled Qt" FORCE)
				break()
			endif()
		endforeach()
	endif()
endif()

find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc photo)
add_library(binarization_core src/Binarizer.cpp)
target_include_directories(binarization_core PUBLIC src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(binarization_core PUBLIC ${OpenCV_LIBS})
option(BUILD_GUI "Build the Qt Quick desktop app" ON)
option(BUILD_APP_PACKAGE "Install a desktop bundle with Qt runtime dependencies" OFF)

if(BUILD_GUI)
	find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Concurrent)
	qt_standard_project_setup(REQUIRES 6.5)
	file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS
		"src/app/*.cpp"
		"src/app/*.h"
		"src/app/*.hpp")
	qt_add_executable(nasti_designer ${app_sources})
	set_target_properties(nasti_designer PROPERTIES
		OUTPUT_NAME "nasti-designer"
		WIN32_EXECUTABLE TRUE
		MACOSX_BUNDLE ${BUILD_APP_PACKAGE}
		MACOSX_BUNDLE_BUNDLE_NAME "NastiDesigner"
		MACOSX_BUNDLE_GUI_IDENTIFIER "cafe.siornya.nastidesigner")
	file(GLOB_RECURSE qml_files CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "qml/*.qml")
	foreach(qml_file IN LISTS qml_files)
		file(RELATIVE_PATH resource_path "${CMAKE_CURRENT_SOURCE_DIR}/qml" "${CMAKE_CURRENT_SOURCE_DIR}/${qml_file}")
		set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${resource_path}")
	endforeach()
	qt_add_qml_module(nasti_designer URI Binarizer VERSION 1.0 QML_FILES ${qml_files})
	target_compile_definitions(nasti_designer PRIVATE BINARIZER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
	target_link_libraries(nasti_designer PRIVATE binarization_core Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent)
	if(BUILD_APP_PACKAGE)
		include(GNUInstallDirs)
		install(TARGETS nasti_designer
			BUNDLE DESTINATION .
			RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
		qt_generate_deploy_qml_app_script(
			TARGET nasti_designer
			OUTPUT_SCRIPT deploy_script)
		install(SCRIPT ${deploy_script})
	endif()
endif()

include(CTest)
if(BUILD_TESTING)
	add_executable(binarizer_tests tests/BinarizerTests.cpp)
	target_link_libraries(binarizer_tests PRIVATE binarization_core)
	add_test(NAME binarizer_core COMMAND binarizer_tests)
endif()
if(BUILD_TESTING AND BUILD_GUI)
	set(controller_test_sources ${app_sources})
	list(REMOVE_ITEM controller_test_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp")
	qt_add_executable(controller_tests tests/ControllerTests.cpp ${controller_test_sources})
	target_include_directories(controller_tests PRIVATE src/app)
	target_compile_definitions(controller_tests PRIVATE BINARIZER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
	target_link_libraries(controller_tests PRIVATE binarization_core Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent)
	add_test(NAME app_smoke COMMAND controller_tests)
	set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
