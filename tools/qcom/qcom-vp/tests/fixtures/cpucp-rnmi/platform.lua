-- Synthetic guests use the existing architectural reset and CEASE fixture.
dofile(assert(os.getenv('QCOM_VP_PLATFORM_DIR'))..'/../tools/qcom/qcom-vp/tests/fixtures/cpucp-reset/platform.lua')
platform.boot.legacy_rnmi=true
