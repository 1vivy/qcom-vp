# Secure-file local-authority experiment

This experiment executes selected original TrustZone instructions from an external matched image.
It exercises the local protected version-table path with explicit host services.
It probes the original UID `0x0b` registry in a constructed empty registry. It does not execute a registered key provider or use RPMB storage.

## Research question

Can the original routines create a protected table, update its version, read it after process restart, and reject changed content?

The experiment uses a generated test identity and an explicit 32-byte test key.
It does not need captured private files or device data.
The program loads firmware from a path that the researcher supplies.
It checks the complete file against the supplied SHA-256 value and the matched image identity.
It does not include Qualcomm firmware bytes.

## Host boundaries and limits

The runner enters original instructions with an initialized test key in the trusted-application context.
This initialization stands in for completed setup before the selected routine.
It does not reproduce the source or derivation of a physical key.

The lookup returns no handler in this constructed registry.
This does not establish whether another startup path registers the service.

The host config boundary selects the local-file mode by returning the declared `cmnlib_gppo_rpmb_enablement` default.
The host reports the optional counter object as absent.
The host implements HMAC-SHA-256 with the supplied test key.
The file adapter rejects symbolic links and non-regular entries.
It stores opaque filename hashes in the supplied directory.
It flushes file data and directory updates before it reports a write.
These host services are explicit inputs, not original firmware services.

The runner stops if original instructions reach the RPMB persistence routine.
The result does not qualify the phone's selected version authority, service registration, root-key source, or secure-file object payloads.
It does not model index or object creation.
The calls to `fsync` do not qualify crash recovery.
An unfinished `.pending` file causes later operations to stop. The runner does not recover it.
The result does not prove that the phone accepts the test key.

## Prerequisites

- Linux.
- Python 3.10 or later.
- The pinned packages in `requirements.txt`.
- The matched TZ ELF image as a local external file.
- A fresh writable directory for experiment state.

## Run the experiment

Run each command as a separate process.
Do not run two commands at the same time against one store.
Use the same image, key, and store directory for all commands.
The `create` command requires an empty store directory.
Set `TZ_IMAGE` to the matched external image path.
Set `TZ_SHA256` to `f6317e33e3daf280d39a7912bb103e8a8840e0130e6ff1a4b158920e5e7e3fa2`.
Set `TEST_KEY_HEX` to a public or locally generated 64-character test key.
The `--test-key-hex` value appears in process arguments. Use test material only.

```sh
set -eu
python3 -m venv .venv-secure-storage
. .venv-secure-storage/bin/activate
python3 -m pip install -r environments/qcom/secure-storage/requirements.txt
STORE=$(mktemp -d)
python3 environments/qcom/secure-storage/workflow.py create \
  --image "$TZ_IMAGE" --sha256 "$TZ_SHA256" --store "$STORE" \
  --test-key-hex "$TEST_KEY_HEX"
python3 environments/qcom/secure-storage/workflow.py set-version \
  --image "$TZ_IMAGE" --sha256 "$TZ_SHA256" --store "$STORE" \
  --test-key-hex "$TEST_KEY_HEX" --version 7
python3 environments/qcom/secure-storage/workflow.py get-version \
  --image "$TZ_IMAGE" --sha256 "$TZ_SHA256" --store "$STORE" \
  --test-key-hex "$TEST_KEY_HEX"
python3 environments/qcom/secure-storage/workflow.py tamper \
  --image "$TZ_IMAGE" --sha256 "$TZ_SHA256" --store "$STORE" \
  --test-key-hex "$TEST_KEY_HEX"
python3 environments/qcom/secure-storage/workflow.py verify-tamper \
  --image "$TZ_IMAGE" --sha256 "$TZ_SHA256" --store "$STORE" \
  --test-key-hex "$TEST_KEY_HEX"
```

The `create` command writes a 593-byte protected `research/vTable` file.
The `set-version` command updates the generated identity to version 7.
The next process must report `observed_version` as 7.
The `tamper` command changes an authenticated byte and leaves the trailing check-mode byte intact.
The `verify-tamper` command must report return `0xf0100001` and remove the corrupt primary file.
A valid backup may remain after rejection.
The command does not claim that firmware recovered from that backup.

## Interpretation

This is a bounded instruction-emulation experiment, not a QBox or SystemC execution.
The successful version read proves that the exercised firmware routine accepted this constructed table, test key, host HMAC, and durable host file.
It does not establish the real UID `0x0b` provider, a hardware root key, or the phone's backend choice.
