-- Reuse the real RV64 CEASE/vector/reset fixture without changing its V1 setup.
dofile(assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..
    '/../tools/qcom/qcom-vp/tests/fixtures/cpucp-reset/platform.lua')
platform.boot.moduletype='qcom,cpucp-boot-control-v2'
-- The inherited setup aperture is 0x18877600+0x90, including offsets 0x14 and 0x18.
