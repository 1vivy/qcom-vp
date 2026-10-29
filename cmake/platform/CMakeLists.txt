cmake_minimum_required(VERSION 3.24)
project(qcom-vp-system LANGUAGES C CXX)
find_package(qbox REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
add_executable(qcom-vp-system "${QCOM_VP_SOURCE}/src/platform.cc")
target_include_directories(qcom-vp-system PRIVATE "${QCOM_VP_SOURCE}/models")
# QBox exports common headers but not its component headers.
get_filename_component(QBOX_INSTALL_PREFIX "${qbox_DIR}/../../.." ABSOLUTE)
foreach(component IN ITEMS ufs ufs_lu irq-ctrl/arm_gicv3 irq-ctrl/arm_gicv3_its global_peripheral_initiator)
    string(MAKE_C_IDENTIFIER "${component}" component_id)
    get_filename_component(component_header "${component}" NAME)
    find_path(QBOX_${component_id}_INCLUDE_DIR
        NAMES "${component_header}.h"
        HINTS "${QBOX_INSTALL_PREFIX}/qemu-components/${component}/include"
        REQUIRED)
    target_include_directories(qcom-vp-system PRIVATE "${QBOX_${component_id}_INCLUDE_DIR}")
endforeach()
find_path(QBOX_RISCV32_INCLUDE_DIR
    NAMES riscv32.h
    HINTS "${QBOX_INSTALL_PREFIX}/qemu-components/cpu_riscv/cpu_riscv32/include"
    REQUIRED)
target_include_directories(qcom-vp-system PRIVATE "${QBOX_RISCV32_INCLUDE_DIR}")
find_path(QBOX_A710_INCLUDE_DIR
    NAMES cortex-a710.h
    HINTS "${QBOX_INSTALL_PREFIX}/qemu-components/cpu_arm/cpu_arm_cortex_a710/include"
    REQUIRED)
target_include_directories(qcom-vp-system PRIVATE "${QBOX_A710_INCLUDE_DIR}")
find_path(QBOX_M7_INCLUDE_DIR
    NAMES cortex-m7.h
    HINTS "${QBOX_INSTALL_PREFIX}/qemu-components/cpu_arm/cpu_arm_cortex_m7/include"
    REQUIRED)
target_include_directories(qcom-vp-system PRIVATE "${QBOX_M7_INCLUDE_DIR}")
set_target_properties(qcom-vp-system PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(qcom-vp-system PRIVATE qbox::qbox OpenSSL::Crypto)
set_target_properties(qcom-vp-system PROPERTIES INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib/libqemu")

find_program(QCOM_VP_CLANG NAMES clang REQUIRED)
find_program(QCOM_VP_LLD NAMES ld.lld REQUIRED)
find_program(QCOM_VP_OBJCOPY NAMES llvm-objcopy REQUIRED)
set(TME_RUNTIME_ADAPTER "${CMAKE_CURRENT_BINARY_DIR}/tme-runtime-adapter")
add_custom_command(
    OUTPUT "${TME_RUNTIME_ADAPTER}.bin"
    BYPRODUCTS "${TME_RUNTIME_ADAPTER}.o" "${TME_RUNTIME_ADAPTER}.elf"
    COMMAND "${QCOM_VP_CLANG}" --target=riscv32-unknown-elf
        -march=rv32imac_zicsr -mabi=ilp32 -mno-relax
        -c "${QCOM_VP_SOURCE}/firmware/qcom/sm88xx/tme-runtime/tme-runtime-adapter.S"
        -o "${TME_RUNTIME_ADAPTER}.o"
    COMMAND "${QCOM_VP_LLD}" -m elf32lriscv --no-relax --entry=_start
        -Ttext=0x234c00 --defsym=runtime_entry=0x234a00
        --defsym=runtime_oem_resume=0x2309a8
        "${TME_RUNTIME_ADAPTER}.o" -o "${TME_RUNTIME_ADAPTER}.elf"
    COMMAND "${QCOM_VP_OBJCOPY}" --only-section=.text -O binary
        "${TME_RUNTIME_ADAPTER}.elf" "${TME_RUNTIME_ADAPTER}.bin"
    DEPENDS "${QCOM_VP_SOURCE}/firmware/qcom/sm88xx/tme-runtime/tme-runtime-adapter.S"
    VERBATIM)
add_custom_target(qcom-vp-tme-runtime-adapter ALL DEPENDS "${TME_RUNTIME_ADAPTER}.bin")
add_dependencies(qcom-vp-system qcom-vp-tme-runtime-adapter)
install(FILES "${TME_RUNTIME_ADAPTER}.bin" DESTINATION share/qcom-vp/firmware/qcom/sm88xx/tme-runtime)

install(TARGETS qcom-vp-system RUNTIME DESTINATION bin)
install(DIRECTORY "${QCOM_VP_SOURCE}/platforms" "${QCOM_VP_SOURCE}/boards" DESTINATION share/qcom-vp)
