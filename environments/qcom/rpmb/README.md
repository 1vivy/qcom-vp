# UFS RPMB persistent-state experiment

This experiment drives the UFS RPMB through the authored AArch64 guest and the real UFS request path.
It does not require OEM firmware or a connected device.
It does not use or qualify a physical RPMB.

## Research question

Does the virtual RPMB keep its key, counter, and media across a new process when the experiment declares persistent state?

The guest branches on the counter-read result.
A fresh state reports NO_KEY, programs a synthetic key, writes two blocks, and reads them back.
A resumed state checks counter 1 and both blocks.
It then checks replay, wrong-key, and tampered-frame rejection.
It reads the counter and blocks again after those rejected requests.
The final guest access reaches `0x600d0000` only after these checks.
A failed guest assertion accesses `0xbad00000` instead.

## Constructed state and limits

The AArch64 guest starts at `0x300000` in constructed RAM.
The composition models a virtual UFS link, six block-backed LUNs, and a 256 KiB RPMB region.
The six LUNs use immutable zero-filled seed files. The launcher copies them into mutable state.
The RPMB starts absent in a fresh run. The QEMU UFS device creates it through the RPMB state-file property.

The manifest declares `rpmb.bin`, `rpmb.bin.lock`, and `rpmb.bin.tmp` as separate state paths.
The lock file remains empty and regular.
The temporary file is a reserved synchronization path. A killed write can leave a partial file there.
The next open removes a regular nonsymlink temporary file while it holds the lock.
It does not overwrite that file during snapshot creation.
A symlink, directory, or device at the temporary path stops realization.

This experiment checks a virtual model contract.
It does not qualify JESD220 behavior against a device.
It does not establish OEM authentication, a hardware key, OEM firmware execution, timing, or physical storage behavior.
The guest key bytes are synthetic test data.

The experiment launcher runs trusted Lua code without a sandbox.
The manifest declares the Lua composition, its helper files, the guest, and all immutable LUN seeds.
The launcher records state-file presence or absence and rejects undeclared files, symlinks, and nonregular files.

## Prerequisites

Run the commands from the repository root.

- `make build` must install `build/install/bin/qcom-vp` and `qcom-vp-system`.
- `clang` with LLD and `llvm-objcopy` must compile the guest.
- Python prepares the inputs. The observation commands also use `jq`, `sha256sum`, `grep`, and `stat`.

## Prepare and run a fresh identity

Use a new preparation directory and output directory for each run.
The preparer compiles the guest and writes the environment manifest.
The manifest stores copied platform helpers and immutable zero-filled seeds under `inputs/`.

```sh
set -eu
WORK=$(mktemp -d build/rpmb.XXXXXX)
python3 environments/qcom/rpmb/prepare.py --out "$WORK/prepared"
set +e
build/install/bin/qcom-vp experiment \
  --environment "$WORK/prepared/environment.toml" \
  --out "$WORK/fresh" \
  --system "$PWD/build/install/bin/qcom-vp-system" \
  --timeout-seconds 60
status=$?
set -e
test "$status" -ne 0
```

The child exits with status 2 after the expected sentinel.
The experiment command therefore returns nonzero.
The result record must report outcome `exited`, exit code `2`, no timeout, and no launcher error.
The stderr log must contain the final access at `0x600d0000` and must not contain `0xbad00000`.

```sh
jq -e -s '.[0] | .outcome == "exited" and .exit_code == 2 and .timed_out == false and .error == null' \
  "$WORK/fresh/result.json"
grep -F 'address=0x600d0000' "$WORK/fresh/stderr.log"
! grep -F 'address=0xbad00000' "$WORK/fresh/stderr.log"
ls "$WORK/fresh/state"
```

The state directory must contain six LUN images, `rpmb.bin`, and `rpmb.bin.lock`.
A clean run must not leave `rpmb.bin.tmp`.
The RPMB file size must be 262240 bytes for the declared 256 KiB region.

```sh
test "$(stat -c %s "$WORK/fresh/state/rpmb.bin")" -eq 262240
test ! -e "$WORK/fresh/state/rpmb.bin.tmp"
test "$(stat -c %s "$WORK/fresh/state/rpmb.bin.lock")" -eq 0
```

## Resume and clone the persistent identity

Both continuation modes use a new output directory.
They copy all declared state and preserve the source run.
The same manifest and system executable must remain in use.

```sh
set +e
build/install/bin/qcom-vp experiment \
  --environment "$WORK/prepared/environment.toml" \
  --resume-from "$WORK/fresh" --out "$WORK/resumed" \
  --system "$PWD/build/install/bin/qcom-vp-system" --timeout-seconds 60
resume_status=$?
build/install/bin/qcom-vp experiment \
  --environment "$WORK/prepared/environment.toml" \
  --clone-from "$WORK/fresh" --out "$WORK/cloned" \
  --system "$PWD/build/install/bin/qcom-vp-system" --timeout-seconds 60
clone_status=$?
set -e
test "$resume_status" -ne 0
test "$clone_status" -ne 0
```

Check each result and stderr with the commands from the fresh-run section.
The resumed and cloned guests must report counter 1 and read the blocks written by the fresh guest.
Their rejected writes must not change `rpmb.bin`.

```sh
sha256sum "$WORK/fresh/state/rpmb.bin" \
  "$WORK/resumed/state/rpmb.bin" "$WORK/cloned/state/rpmb.bin"
test "$(sha256sum "$WORK/fresh/state/rpmb.bin" | cut -d ' ' -f 1)" = \
     "$(sha256sum "$WORK/resumed/state/rpmb.bin" | cut -d ' ' -f 1)"
test "$(sha256sum "$WORK/fresh/state/rpmb.bin" | cut -d ' ' -f 1)" = \
     "$(sha256sum "$WORK/cloned/state/rpmb.bin" | cut -d ' ' -f 1)"
```

## Reject invalid backing files

The preparer accepts an optional immutable RPMB seed for negative tests.
This seed gives the guest a pre-existing backing file at startup.
Each test uses a separate environment manifest and a new output directory.

```sh
cp "$WORK/fresh/state/rpmb.bin" "$WORK/rpmb-truncated.bin"
truncate -s 1000 "$WORK/rpmb-truncated.bin"
python3 environments/qcom/rpmb/prepare.py \
  --out "$WORK/bad-truncated" --rpmb-seed "$WORK/rpmb-truncated.bin"
set +e
build/install/bin/qcom-vp experiment \
  --environment "$WORK/bad-truncated/environment.toml" \
  --out "$WORK/reject-truncated" \
  --system "$PWD/build/install/bin/qcom-vp-system" --timeout-seconds 60
status=$?
set -e
test "$status" -ne 0
grep -F 'geometry requires 262240' "$WORK/reject-truncated/stderr.log"
```

To check integrity rejection, change one media byte in a copy of the valid file.
The media starts at byte 96.

```sh
cp "$WORK/fresh/state/rpmb.bin" "$WORK/rpmb-corrupt.bin"
printf '\377' | dd of="$WORK/rpmb-corrupt.bin" bs=1 seek=100 conv=notrunc status=none
python3 environments/qcom/rpmb/prepare.py \
  --out "$WORK/bad-corrupt" --rpmb-seed "$WORK/rpmb-corrupt.bin"
set +e
build/install/bin/qcom-vp experiment \
  --environment "$WORK/bad-corrupt/environment.toml" \
  --out "$WORK/reject-corrupt" \
  --system "$PWD/build/install/bin/qcom-vp-system" --timeout-seconds 60
status=$?
set -e
test "$status" -ne 0
grep -F 'failed its integrity check' "$WORK/reject-corrupt/stderr.log"
```

To check geometry rejection, seed a 512 KiB profile with the 256 KiB file.
The preparer updates the helper input and its hash.

```sh
python3 environments/qcom/rpmb/prepare.py \
  --out "$WORK/bad-geometry" --rpmb-seed "$WORK/fresh/state/rpmb.bin" \
  --rpmb-size-kib 512
set +e
build/install/bin/qcom-vp experiment \
  --environment "$WORK/bad-geometry/environment.toml" \
  --out "$WORK/reject-geometry" \
  --system "$PWD/build/install/bin/qcom-vp-system" --timeout-seconds 60
status=$?
set -e
test "$status" -ne 0
grep -F 'geometry requires 524384' "$WORK/reject-geometry/stderr.log"
```

To check zero-length rejection, make an empty seed and prepare it as immutable input.

```sh
: > "$WORK/rpmb-empty.bin"
python3 environments/qcom/rpmb/prepare.py \
  --out "$WORK/bad-empty" --rpmb-seed "$WORK/rpmb-empty.bin"
set +e
build/install/bin/qcom-vp experiment \
  --environment "$WORK/bad-empty/environment.toml" \
  --out "$WORK/reject-empty" \
  --system "$PWD/build/install/bin/qcom-vp-system" --timeout-seconds 60
status=$?
set -e
test "$status" -ne 0
grep -F 'holds 0 bytes, the geometry requires 262240' "$WORK/reject-empty/stderr.log"
```

## Check writer exclusion and stale temporary state

The lock applies to the persistent `rpmb.bin.lock` file.
A second process must not access the same state while a writer holds that lock.
The following direct QEMU invocation uses the same declared input and state roots as the experiment.
It tests only lock exclusion, not the experiment result checker.

```sh
python3 - "$WORK" "$PWD/build/install/bin/qcom-vp-system" <<'PY'
import fcntl, os, subprocess, sys
from pathlib import Path

work = Path(sys.argv[1]).resolve()
run = work / "lock-check"
run.mkdir()
environment = dict(os.environ, QCOM_VP_ENV_ROOT=str(work / "fresh/inputs"),
                   QCOM_VP_RUN_ROOT=str(run), QCOM_VP_STATE_ROOT=str(work / "fresh/state"))
with (work / "fresh/state/rpmb.bin.lock").open("r+b") as lock:
    fcntl.lockf(lock, fcntl.LOCK_EX)
    result = subprocess.run([sys.argv[2], "-l", str(work / "fresh/inputs/platform.lua")],
                            env=environment, capture_output=True, text=True, timeout=60)
assert result.returncode != 0 and "already in use" in result.stderr, result.stderr
print("RPMB writer exclusion passed")
PY
```

The child stderr must report that the RPMB state file is already in use.
The test must not change the state directory.

A regular interrupted `rpmb.bin.tmp` is a declared reserved path.
The next device open checks that the path is regular and not a symlink under the lock.
It then removes the temporary file and reads the last committed `rpmb.bin`.
A symlink, directory, device, or path changed during inspection causes realize failure.

## Interpretation

A passing guest result establishes only the virtual behaviors that this guest checks.
The file hashes show whether persistent state changed across the rejected requests.
They do not prove physical RPMB behavior or secure hardware key storage.
A directory-flush failure after rename can leave the new state visible while the device reports failure.
That failure result has indeterminate crash durability and does not count as a successful request.
