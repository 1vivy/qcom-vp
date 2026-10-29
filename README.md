# qcom-vp

qcom-vp provides composable environments for Qualcomm hardware and firmware research through QBox.
Researchers model components and use the resulting environments for development, debugging, and security testing.
Device configurations and complete boot sequences are results of composition, not the limits of the project.

The SM8850 reference reaches the OEM kernel and produces early UART output.
Its next unsupported access reads the timer at `0x17810008`.
The SM8845 reference supports bounded component and firmware experiments without requiring kernel handoff.
See [execution evidence](docs/qcom/sm88xx/oplus/bringup.md#status) for the inputs, observations, and limits.

Shared components live under `qcom/`, with family recipes under `sm88xx/` and SoC or OEM differences below them.
This catalog includes inferred patterns and provisional component identities.
The [architecture](docs/qcom/sm88xx/architecture.md#working-catalog-patterns) explains how shared recipes and specific data fit together.

The repository does not distribute vendor firmware.
The [IPCC example](environments/qcom/ipcc/README.md) uses authored guests and requires no phone or OEM package.

## Build

The documented host environment is Linux.

| Workflow | Requirements |
| --- | --- |
| Build | GNU Make, CMake 3.24 or later, Ninja, C and C++17 compilers, Rust 1.85 or later, Cargo, Git, and pkg-config |
| Host utilities | GNU coreutils, including `install`, `realpath`, `timeout`, and `sha256sum` |
| Native dependencies | OpenSSL 3 development files and the pinned [QBox](https://github.com/qualcomm/qbox) dependencies, including Asio, SDL, and GLib development files |
| Firmware adapter and guest compilation | Clang, LLD, and `llvm-objcopy`. The normal build also needs these tools. |
| Automatic OEM observation and EFI decoding | `xz` |
| Compressed log analysis and retention | `zstd` |
| IPCC example preparation and checks | `rustc`, `sha256sum`, and `jq`, in addition to the guest compiler tools |

```sh
make build
make test
export PATH="$PWD/build/install/bin:$PATH"
```

The default build installs both executables under `build/install/bin`.
`BUILD_DIR` selects another build directory. `PREFIX` selects the installation directory.
Without `PREFIX`, installation uses `<BUILD_DIR>/install`.
`make test` uses the installation selected by that invocation.

```sh
make build BUILD_DIR=build/research PREFIX="$PWD/build/research-install"
make test BUILD_DIR=build/research PREFIX="$PWD/build/research-install"
```

The CMake cache variable `QCOM_VP_DEPS` selects the dependency workspace.
Its default is `~/.cache/qcom-vp`, not a firmware or evidence store.
`package-lock.cmake` pins QBox, libqemu, SystemC, and SCP.

## Research execution

The [environment runbook](docs/environments.md) describes external Lua composition, input identities, and experiment outcomes.
Start with the [IPCC procedure](environments/qcom/ipcc/README.md) for a complete experiment with repeat and failure checks.
The [RPMB procedure](environments/qcom/rpmb/README.md) adds durable state, authenticated requests, resume, and clone checks without vendor firmware.
The [local-authority procedure](environments/qcom/secure-storage/README.md) exercises original TZ routines with an external image and explicit test inputs.

For another experiment, set `ENVIRONMENT_MANIFEST` to the path of its manifest.
Use a new output directory:

```sh
qcom-vp experiment --environment "$ENVIRONMENT_MANIFEST" --out build/experiment-1
```

Lua defines the hardware and firmware arrangement.
The launcher checks and copies declared inputs before execution.
An experiment-specific observation establishes the result. Process success alone does not establish guest behavior or kernel entry.

The following sections describe the separate OEM boot workflow.

## Prepare firmware

1. Copy `firmware/qcom/sm88xx/oplus/example.toml` to an untracked local file.
2. Set each input path and SHA-256 to identify your firmware.
3. Set `FIRMWARE_MANIFEST` to the path of that local manifest.
4. Run preparation with a new output directory.

```sh
qcom-vp prepare --firmware "$FIRMWARE_MANIFEST" --out build/prepared
```

The manifest uses schema version 1. Relative paths refer to the manifest directory.
[Firmware inputs](docs/qcom/sm88xx/oplus/firmware-inputs.md) explains each input, the ROM offset keys, and the board file.
Preparation checks the ROM and package hashes before it creates output.
It records the consumed archive members and their hashes.
It assembles six sparse LUN files, applies ordered patches, checks both GPT copies, and compares the resulting source extents.
It creates `prepared.json` only after these checks pass.
A failed preparation can retain its output directory. The CLI never overwrites that directory.

### Optional sequencer trust

A manifest can pin the sequencer trust inputs.
Set `sequencer_root` and `sequencer_entitlement_sha256` together.
The root file contains 136 bytes: the P-521 public key x and y coordinates, 68 bytes each, big-endian.
The 64-character hexadecimal digest identifies the accepted issuer-signed entitlement TLV.

Preparation checks the root hash, copies the file to `sequencer-root.bin`, and records its identity.
Execution checks the copy again before the virtual TME boot status model consumes it and the digest.
Omit both fields only for status-only use. An empty trust configuration makes sequencer authentication fail closed.
Keep vendor keys and entitlements outside version control.

### Optional OEM root

The `oem_root` input identifies the complete DER certificate for the OEM root.
Preparation checks its SHA-256 and copies it to `oem-root.der`.
Execution checks the private copy before it creates mutable run output.
It then computes SHA-384 for the fuse profile.
The TME firmware consumes that digest through the modeled `PK_HASH` registers.
The host does not adopt a root from a runtime guest image.

Omit this input only for earlier hardware investigations.
Without the input, root-hash reads fail instead of returning an empty or permissive hash.
The sequencer's P-521 issuer and the OEM image root are separate trust inputs.

### Prepared output


Prepared output uses schema version 2.
It records UFS boot capability and each provisioned boot ID separately.
Recreate older prepared directories before execution.
The installed board files explicitly select boot ID one as virtual reset policy.

Both selected OEM packages require `IMAGES/sec.dat`. Neither package contains that member.
The package metadata provides no exclusion or substitution rule.
Preparation reports this error unless the manifest explicitly selects omission:

```text
Required package member is absent: IMAGES/sec.dat. Do not substitute a different security image.
```

The available `sec_smt.dat` is not a verified replacement.
The reference experiment selects `omit_secdata = true`.
That setting omits only `sec.dat` writes to `secdata` on LUN4.
The report records each omitted XML record. The partition remains unpopulated.
The omission does not copy physical-device data or bypass authentication.

## Execute firmware

```sh
qcom-vp run --prepared build/prepared --out build/run --stop-at kernel-entry
```

The CLI locates `qcom-vp-system` beside itself.
It checks prepared identities and creates independent mutable copies of the LUN files.
The run directory contains the Lua configuration, console output, system output, and instruction traces.
The host stops the process if it exceeds the default timeout of 180 seconds.
Neither a zero process status nor an entry marker can establish a kernel handoff.
The host also requires execution evidence and guest snapshots that match the selected kernel and the AArch64 boot contract.
The host observer uses hash-bound firmware sites through a private GDB socket.
Results apply to the specific inputs and revisions in [execution evidence](docs/qcom/sm88xx/oplus/bringup.md).

The optional `--apss-gdb`, `--tme-gdb`, and `--cpucp-gdb` arguments select distinct debugger ports.
`--debug-pause` requires all three ports and pauses each instance at reset.

## Authentication policy

The OEM CLI accepts only `auth = "retail"`. This selection does not establish secure-boot qualification.
Authentication stubs and silent authentication bypasses are not supported.

## Evidence references

Component documentation records source identities, observations, inferred contracts, and unresolved questions.
The [evidence guide](docs/evidence.md) defines the citation format and local configuration.
[docs/references.toml](docs/references.toml) names evidence stores without choosing their locations on your machine.
Private stores are optional for building the project or running authored examples.

To check retained source identities, first configure the required local stores:

```sh
cargo run --locked -p qcom-vp -- references
```

The checker reports missing stores and identity mismatches as errors.
A remote-only citation does not mean that the checker inspected the remote content.
This command checks evidence references, not redistribution permissions or complete hardware fidelity.

## Further information

- [Architecture](docs/qcom/sm88xx/architecture.md)
- [Execution evidence and limits](docs/qcom/sm88xx/oplus/bringup.md)
- [Functional substitutes](docs/qcom/sm88xx/stubs.md)
- [Implementation decisions](docs/qcom/sm88xx/decisions.md)
- [Hardware fact gathering](docs/qcom/sm88xx/oplus/infiniti/hardware-evidence.md)
- [Firmware inputs](docs/qcom/sm88xx/oplus/firmware-inputs.md)
- [Environment runbook](docs/environments.md)
- [Environment inventory](docs/environment-inventory.md)
- [Research environments plan](docs/research-environments-plan.md)

## License

Project code uses the [BSD-3-Clause-Clear license](LICENSE).
Dependencies retain their own licenses.
