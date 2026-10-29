# QBox and libqemu source identities.
set(QCOM_VP_QBOX_REVISION 0a66507c5d8af7a0cd997a2517364a6b678e529e)
set(QCOM_VP_LIBQEMU_REVISION 6ef1d0ee35929a157e8027db8d246412ee0df95c)
set(QCOM_VP_LIBQEMU_PATCHES
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0001-export-readonly-memory.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0002-export-cpu-observation.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0003-implement-ufs-boot-lun.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0004-implement-sifive-cease.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0005-implement-ufs-device-reset.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0006-retain-ufs-link-negotiation.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0007-implement-qcom-clic.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0008-implement-single-partition-mpam.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0009-implement-ufs-device-readiness.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0010-expose-ufs-configuration.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0011-enable-ufs-hibernation-flush.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0012-configure-fp-reset.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0013-implement-tme-retained-power.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0014-stop-riscv-io-lookahead.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0015-implement-sifive-cache-operations.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0016-expose-riscv-wfi-output.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0017-implement-legacy-rnmi.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0018-implement-qcom-identification-registers.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0019-implement-mpamhcr-el2.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0020-expose-virtual-list-register-count.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0021-implement-ete-programmer-words.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0022-preserve-split-mmio-translations.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0023-preserve-m-profile-reset-attributes.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0024-report-peer-ufs-hs-rate.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0025-report-ufs-rpmb-readiness.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0026-implement-ufs-rpmb.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0027-expose-pmu-counter-count.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0028-add-bulk-memory-access.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0029-implement-qcom-amu.patch"
  "${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0030-persist-ufs-rpmb.patch")
if(NOT COMMAND CPMDeclarePackage)
  return()
endif()
set(QCOM_VP_LIBQEMU_IDENTITY "${QCOM_VP_LIBQEMU_REVISION}")
foreach(patch IN LISTS QCOM_VP_LIBQEMU_PATCHES)
  file(SHA256 "${patch}" digest)
  string(APPEND QCOM_VP_LIBQEMU_IDENTITY ";${digest}")
endforeach()
string(SHA256 QCOM_VP_LIBQEMU_CACHE_KEY "${QCOM_VP_LIBQEMU_IDENTITY}")

CPMDeclarePackage(SystemCLanguage
  NAME SystemCLanguage
  GIT_TAG febde7a3e007ce3030cf574a4cb6fcc13d0ed820
  GIT_REPOSITORY ${SYSTEMC_GIT}
  GIT_SHALLOW OFF
  OPTIONS "ENABLE_SUSPEND_ALL" "ENABLE_PHASE_CALLBACKS")
CPMDeclarePackage(SCP
  NAME SCP
  GIT_TAG 0821c329a1df363503f5ea12309cc5fa2c064530
  GIT_REPOSITORY ${SCP_GIT}
  GIT_SHALLOW OFF)
CPMDeclarePackage(qemu
  NAME libqemu
  GIT_REPOSITORY ${LIBQEMU_GIT}
  GIT_TAG ${QCOM_VP_LIBQEMU_REVISION}
  CUSTOM_CACHE_KEY ${QCOM_VP_LIBQEMU_CACHE_KEY}
  GIT_SUBMODULES CMakeLists.txt
  GIT_SHALLOW OFF
  PATCH_COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0001-export-readonly-memory.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0002-export-cpu-observation.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0003-implement-ufs-boot-lun.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0004-implement-sifive-cease.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0005-implement-ufs-device-reset.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0006-retain-ufs-link-negotiation.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0007-implement-qcom-clic.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0008-implement-single-partition-mpam.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0009-implement-ufs-device-readiness.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0010-expose-ufs-configuration.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0011-enable-ufs-hibernation-flush.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0012-configure-fp-reset.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0013-implement-tme-retained-power.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0014-stop-riscv-io-lookahead.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0015-implement-sifive-cache-operations.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0016-expose-riscv-wfi-output.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0017-implement-legacy-rnmi.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0018-implement-qcom-identification-registers.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0019-implement-mpamhcr-el2.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0020-expose-virtual-list-register-count.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0021-implement-ete-programmer-words.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0022-preserve-split-mmio-translations.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0023-preserve-m-profile-reset-attributes.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0024-report-peer-ufs-hs-rate.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0025-report-ufs-rpmb-readiness.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0026-implement-ufs-rpmb.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0027-expose-pmu-counter-count.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0028-add-bulk-memory-access.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0029-implement-qcom-amu.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR>
    -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/libqemu/0030-persist-ufs-rpmb.patch
    -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyPatch.cmake)
