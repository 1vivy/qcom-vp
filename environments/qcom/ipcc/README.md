# Synthetic IPCC environment

This example exercises the authored IPCC guest pair without an OEM firmware image or a phone.
It uses the fixture in `tools/qcom/qcom-vp/tests/fixtures/ipcc` and the shared APSS GIC helper in `platforms/qcom/apss-gic.lua`.
The preparation tool copies those inputs and composes a schema-1 environment manifest.
It does not create a second source copy of either guest or the platform composition.

## Research question

Does the constructed APSS producer and RV32 TME guest exercise source-aware IPCC routing, shared queue state, interrupts, and aliases before the intentional unmapped access?

The guest checks queue configuration, delivery, source IDs, interrupt handling, aliases, and local state.
A failed guest check accesses `0xbad00000` and does not reach the final sentinel.
The successful check path then accesses `0x600d0000` by design.
The checker requires that exact sentinel and the expected child exit status.

## Constructed state and limits

The platform sets the APSS Cortex-A53 reset vector to `0x300000` and declares EL3 and EL2 support.
It sets the RV32 TME CLIC CPU reset vector to `0x200000`.
The composition defines synthetic memory maps, CPUs, IPCC models, interrupt paths, and aliases.
This is a constructed reset state, not a state produced by PBL, XBL, QSEE, UEFI, ABL, or another OEM stage.

The platform describes a bounded virtual composition.
It does not model the complete device, OEM authentication, authorization, trust policy, or physical behavior.
Do not treat its result as boot success, kernel entry, physical equivalence, or security qualification.

The APSS guest binary has a `0x1000`-byte prefix of `0xa5` bytes.
The copied Lua composition loads its guest bytes at guest address `0x300000` with `bin_file_offset=0x1000` and the compiled guest size as `bin_file_size`.
The file prefix is not mapped at the guest address.
This demonstrates the separation between file layout and guest address.

The launcher executes Lua as trusted host code.
The experiment is not a sandbox.
Declare external dependencies in the environment that requires them.
The launcher preserves the child output and records its process outcome.
A child timeout or failure remains an experiment-command failure.

## Prerequisites

Run all commands from the repository root.

- `rustc` compiles the standard-library-only preparation and checker tool.
- `clang` with LLD compiles the authored APSS and TME guests.
- `llvm-objcopy` converts each ELF file to a raw guest image.
- `sha256sum` records full-file hashes in the environment manifest.
- `jq` checks `result.json` in the workflow checker.
- `build/install/bin/qcom-vp` and `build/install/bin/qcom-vp-system` must exist.
Run `make build` first if these installed binaries do not exist.

The preparation tool uses the same target triples and guest flags as the IPCC test in `tools/qcom/qcom-vp/tests/platform.rs`.
It builds the default `CASE=0` guest.
The guest sources remain unchanged.

## Run the experiment

Use a fresh parent directory for each run.
The preparation directory and experiment output directory must not already exist.
Run the numbered steps in the same shell session because later steps use the variables from step 1.

1. Compile the host workflow tool and prepare a new input directory.

   ```sh
   set -eu
   TOOL_DIR=$(mktemp -d build/ipcc-tools.XXXXXX)
   RUN_DIR=$(mktemp -d build/ipcc-run.XXXXXX)
   rustc --edition=2021 environments/qcom/ipcc/workflow.rs -o "$TOOL_DIR/ipcc-workflow"
   "$TOOL_DIR/ipcc-workflow" prepare --out "$RUN_DIR/prepared"
   ```

   Preparation compiles the guests, pads the APSS guest, copies `apss-gic.lua`, and writes `environment.toml`.
   It hashes the complete snapshot files with `sha256sum`.
   The manifest paths are relative to `environment.toml`.
   The tool makes three exact, count-checked replacements in the copied fixture Lua.
   It switches the input-root variable to `QCOM_VP_ENV_ROOT`.
   It points the helper directory at that snapshot root.
   It changes the producer load entry to the padded file and its byte slice.
   Each source string must occur once, or preparation stops.

2. Run `qcom-vp experiment` with a fresh output directory.

   ```sh
   set +e
   build/install/bin/qcom-vp experiment \
     --environment "$RUN_DIR/prepared/environment.toml" \
     --out "$RUN_DIR/experiment" \
     --system "$PWD/build/install/bin/qcom-vp-system" \
     --timeout-seconds 30
   experiment_status=$?
   set -e
   if [ "$experiment_status" -eq 0 ]; then
     echo "The expected sentinel must leave a nonzero child status." >&2
     exit 1
   fi
   ```

   The command must return nonzero because the child exits with status `2` after the intentional sentinel.
   The experiment result is in `"$RUN_DIR/experiment/result.json"`.
   The launcher snapshots inputs under `"$RUN_DIR/experiment/inputs/"`.
   It records child output in `stdout.log` and `stderr.log`.

3. Check the process result and the stderr sentinel.

   ```sh
   "$TOOL_DIR/ipcc-workflow" check --out "$RUN_DIR/experiment"
   ```

   The checker uses `jq` to require schema `1`, outcome `exited`, child exit code `2`, no signal, no timeout, and no error.
   It requires exactly one `UNMODELED_ACCESS` line with the sole address token `address=0x600d0000`.
   It rejects the guest failure address `0xbad00000`.
   It does not infer kernel entry or security qualification.

4. Repeat from fresh directories.

   ```sh
   RUN_DIR_2=$(mktemp -d build/ipcc-repeat.XXXXXX)
   "$TOOL_DIR/ipcc-workflow" prepare --out "$RUN_DIR_2/prepared"
   set +e
   build/install/bin/qcom-vp experiment \
     --environment "$RUN_DIR_2/prepared/environment.toml" \
     --out "$RUN_DIR_2/experiment" \
     --system "$PWD/build/install/bin/qcom-vp-system" \
     --timeout-seconds 30
   experiment_status=$?
   set -e
   if [ "$experiment_status" -eq 0 ]; then
     echo "The expected sentinel must leave a nonzero child status." >&2
     exit 1
   fi
   "$TOOL_DIR/ipcc-workflow" check --out "$RUN_DIR_2/experiment"
   ```

5. Exercise an incorrect client identity in a separate composition.

   ```sh
   NEGATIVE=$(mktemp -d build/ipcc-negative.XXXXXX)
   cp -a tools/qcom/qcom-vp/tests/fixtures/ipcc "$NEGATIVE/fixture"
   ```

   In the copied `platform.lua`, change the single `local_index=6,` assignment to `local_index=5,`.
   Do not change the original fixture or the guest assertions.
   Preparation calculates new hashes for this deliberate configuration change.

   ```sh
   "$TOOL_DIR/ipcc-workflow" prepare \
     --fixture "$NEGATIVE/fixture" --out "$NEGATIVE/prepared"
   set +e
   build/install/bin/qcom-vp experiment \
     --environment "$NEGATIVE/prepared/environment.toml" \
     --out "$NEGATIVE/experiment" \
     --system "$PWD/build/install/bin/qcom-vp-system" \
     --timeout-seconds 30
   experiment_status=$?
   set -e
   test "$experiment_status" -ne 0
   if "$TOOL_DIR/ipcc-workflow" check --out "$NEGATIVE/experiment"; then
     echo "The checker accepted the incorrect client identity." >&2
     exit 1
   fi
   ```

   The APSS guest must report the bad-state access at `0xbad00000`, not the successful sentinel.

The successful checker output means only that the recorded process outcome and stderr match this experiment's expected sentinel contract.
It does not qualify the model against a physical device.

## Scoped catalog verification, 2026-10-29

The migrated procedure ran twice from fresh preparation directories.
Both manifests had SHA-256 `a56bf46fdf6e4b29e8d51df29c3315793af4640dc93c0245389448d194e9aefc`.
Both platform executions reached the expected sentinel, and both checkers accepted their process outcomes.
The input snapshot now preserves the helper path `qcom/apss-gic.lua`.
The [catalog verification record](../../../docs/environment-inventory.md#catalog-migration-verification-2026-10-29) identifies the retained results and executable hashes.

## Observed verification, 2026-10-28

The documented preparation and execution procedure ran twice in fresh directories.
Both complete environment manifests had SHA-256 `a313cdefe31924f969c7cfb07db93d7908548119bb8ef7f9d00180515c988011`.
The selected system executable had SHA-256 `2f5cb8667ddee12a584c4d953712543cf089a672957766de89983d33c88f04e2`.
The host used Clang, LLD, and llvm-objcopy 23.1.4, rustc 1.99.0, and jaq 3.1.1 through `jq`.

| Scenario | Observed result |
| --- | --- |
| First execution | Child exit 2, one APSS write at `0x600d0000`, value `0x3d`, checker accepted |
| Independent preparation and repeat | Same manifest hash, same expected sentinel, checker accepted |
| Incorrect client identity | Changed `local_index=6` to `local_index=5` in a separate fixture copy. The guest wrote `0xbad00000`, value 3. The checker rejected it. |
| Changed input without a hash update | The launcher recorded `setup_error`, a hash mismatch, and no invocation. |
| Paused processors | Both instances used `qemu_args='-S'` with an updated input hash. The one-second deadline produced `timeout`, signal 9, and checker rejection. |

The wrong-client experiment changed the composition before preparation.
Its source inputs passed identity checks. The observed failure therefore exercised the guest's contract checks, not hash rejection.
The paused-processor experiment exercised the real platform process, not a replacement host program.
These checks establish the bounded execution and observation paths only.

The final launcher also passed with deliberately incorrect inherited input-root variables.
It replaced its own root variables and removed the earlier fixture-root variables.
The recorded inherited-variable map was empty.

Retained records:

- First result: `artifacts:2026-10-28-environment-rework/run1/result.json sha256=b55cf0f71356416bb437d47d3f775a72c40e48de4ded53a578f5c92b42e80dfa`.
- Repeat result: `artifacts:2026-10-28-environment-rework/run2/result.json sha256=9212d7a33b10444dbbefdeba794b144bac1fd78d11dc93a7208d9fa3b415766c`.
- Guest-negative result: `artifacts:2026-10-28-environment-rework/run-wrong-client/result.json sha256=629aa6a4d184480d5d485e8a4a9cf17392ea5d0d104ea6a099821325a97179ee`.
- Timeout result: `artifacts:2026-10-28-environment-rework/run-timeout/result.json sha256=402ad1932ed1fc684f9e58a5df330b81e7d8cc3b6f20da5801d235a988743330`.
- Changed-input result: `artifacts:2026-10-28-environment-rework/run-tamper/result.json sha256=882f6b3355c573267fe3645b1311ce6633eec76abfe5b9ef0a06be08e8da6c68`.
- Final launcher result: `artifacts:2026-10-28-environment-rework/run-final/result.json sha256=5c66eb8d2f13ab258d0ea0ca520094cd416f90bb489f80b960c9e1c62cf62cc2`.
- Expected sentinel: `artifacts:2026-10-28-environment-rework/run-final/stderr.log sha256=244c640dcd477c9af54f44501b10b3462d3c5b85a704dcf72114d02d83cc6bc7`.
- Negative sentinel: `artifacts:2026-10-28-environment-rework/run-wrong-client/stderr.log sha256=810008b8e4a963fb45f7009564522c19dce438a9fb671038e197c241f883f0d2`.
