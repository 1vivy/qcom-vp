BUILD_DIR ?= build
JOBS ?= 4
CARGO ?= cargo
CARGO_TARGET_DIR ?= $(CURDIR)/target
PREFIX ?= $(BUILD_DIR)/install
PREFIX_ABS := $(shell realpath -m -- "$(PREFIX)")
ifeq ($(strip $(PREFIX_ABS)),)
$(error Cannot resolve PREFIX. GNU realpath is required)
endif
RUST_CLI := $(CARGO_TARGET_DIR)/release/qcom-vp

.PHONY: build test

build:
	cmake -S . -B "$(BUILD_DIR)" -G Ninja "-DCMAKE_INSTALL_PREFIX=$(PREFIX_ABS)"
	cmake --build "$(BUILD_DIR)" --parallel $(JOBS)
	CARGO_TARGET_DIR="$(CARGO_TARGET_DIR)" $(CARGO) build --release --locked
	install -d "$(PREFIX_ABS)/bin"
	install -m 755 "$(RUST_CLI)" "$(PREFIX_ABS)/bin/qcom-vp"

test: build
	QCOM_VP_TEST_PREFIX="$(PREFIX_ABS)" CARGO_TARGET_DIR="$(CARGO_TARGET_DIR)" $(CARGO) test --locked -- --include-ignored
