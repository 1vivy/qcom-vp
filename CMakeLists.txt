cmake_minimum_required(VERSION 3.24)
project(qcom-vp LANGUAGES C CXX)
include(ExternalProject)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Local installation" FORCE)
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/install-prefix.txt" CONTENT "${CMAKE_INSTALL_PREFIX}\n")
set(QCOM_VP_DEPS "$ENV{HOME}/.cache/qcom-vp" CACHE PATH "External dependency workspace")
include("${CMAKE_SOURCE_DIR}/package-lock.cmake")
ExternalProject_Add(qbox_upstream
  PREFIX "${QCOM_VP_DEPS}/qbox-project"
  SOURCE_DIR "${QCOM_VP_DEPS}/qbox-src"
  BINARY_DIR "${QCOM_VP_DEPS}/qbox-build"
  GIT_REPOSITORY https://github.com/qualcomm/qbox.git
  GIT_TAG ${QCOM_VP_QBOX_REVISION}
  GIT_SHALLOW FALSE
  UPDATE_DISCONNECTED TRUE
  PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR> -DREVISION=${QCOM_VP_QBOX_REVISION}
    -P ${CMAKE_SOURCE_DIR}/cmake/ResetSource.cmake
    COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_SOURCE_DIR}/patches/qbox/0001-preserve-memory-transaction-attributes.patch
    -P ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_SOURCE_DIR}/patches/qbox/0002-observe-cpu-and-invalidate-code.patch
    -P ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_SOURCE_DIR}/patches/qbox/0003-preserve-target-transaction-attributes.patch
    -P ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_SOURCE_DIR}/patches/qbox/0004-borrow-cross-instance-handles.patch
    -P ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_SOURCE_DIR}/patches/qbox/0005-map-native-cpu-clock-epochs.patch
    -P ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_SOURCE_DIR}/patches/qbox/0006-join-cpu-before-destruction.patch
    -P ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_SOURCE_DIR}/patches/qbox/0007-borrow-cpu-hint-handle.patch
    -P ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND} -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_SOURCE_DIR}/patches/qbox/0008-add-bulk-memory-access.patch
    -P ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=RelWithDebInfo
    "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    "-DPKG_LOCK=${CMAKE_SOURCE_DIR}/package-lock.cmake"
    "-DCPM_SOURCE_CACHE=${QCOM_VP_DEPS}/packages"
    -DENABLE_PYTHON_BINDER=OFF -DGS_ENABLE_VIRGLRENDERER=OFF
    -DGS_ENABLE_VIRCLRENDERER=OFF -DBUILD_TESTING=OFF
    "-DLIBQEMU_TARGETS=aarch64|riscv32|riscv64"
  LIST_SEPARATOR |
)
get_property(qbox_steps TARGET qbox_upstream PROPERTY _EP_STEPS)
set(qbox_patch_step patch)
if("patch_disconnected" IN_LIST qbox_steps)
  set(qbox_patch_step patch_disconnected)
endif()
ExternalProject_Add_StepDependencies(qbox_upstream ${qbox_patch_step}
  "${CMAKE_SOURCE_DIR}/patches/qbox/0001-preserve-memory-transaction-attributes.patch"
  "${CMAKE_SOURCE_DIR}/patches/qbox/0002-observe-cpu-and-invalidate-code.patch"
  "${CMAKE_SOURCE_DIR}/patches/qbox/0003-preserve-target-transaction-attributes.patch"
  "${CMAKE_SOURCE_DIR}/patches/qbox/0004-borrow-cross-instance-handles.patch"
  "${CMAKE_SOURCE_DIR}/patches/qbox/0005-map-native-cpu-clock-epochs.patch"
  "${CMAKE_SOURCE_DIR}/patches/qbox/0006-join-cpu-before-destruction.patch"
  "${CMAKE_SOURCE_DIR}/patches/qbox/0007-borrow-cpu-hint-handle.patch"
  "${CMAKE_SOURCE_DIR}/patches/qbox/0008-add-bulk-memory-access.patch"
  "${CMAKE_SOURCE_DIR}/package-lock.cmake" ${QCOM_VP_LIBQEMU_PATCHES})
ExternalProject_Add_StepDependencies(qbox_upstream configure
  "${CMAKE_SOURCE_DIR}/package-lock.cmake" ${QCOM_VP_LIBQEMU_PATCHES})
ExternalProject_Add_StepDependencies(qbox_upstream install
  "${CMAKE_BINARY_DIR}/install-prefix.txt")
ExternalProject_Add(platform
  DEPENDS qbox_upstream
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/cmake/platform"
  BINARY_DIR "${CMAKE_BINARY_DIR}/platform"
  DOWNLOAD_COMMAND ""
  CMAKE_ARGS "-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}"
    "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
    "-DQCOM_VP_SOURCE=${CMAKE_SOURCE_DIR}"
  BUILD_ALWAYS TRUE
)
ExternalProject_Add_StepDependencies(platform install
  "${CMAKE_BINARY_DIR}/install-prefix.txt")
