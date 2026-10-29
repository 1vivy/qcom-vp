# Evidence references

A reference names its source by identity. The check in `qcom-vp references`
compares the references in root-level Markdown, `docs/`, and `models/` with
the manifest in `docs/references.toml`. The check covers only these paths. It
does not audit the rest of the tree and it does not certify publication.

## What belongs in component evidence

Component evidence in `models/` component directories and in `docs/` records
these items. A component directory usually holds an `evidence.md` file.

- **Observation**: what a probe, dump, or completed run showed. Give the
  input, the command, and the artifact.
- **Inference**: what the observation supports, marked as an inference when
  the observation does not establish it directly.
- **Implemented effect and limit**: the behavior that the model or code
  provides, and the behavior that remains absent.
- **Source identity**: a permalink with a commit, `<store>@<commit>:<path>`,
  or `<store>:<path> sha256=<digest>` for an untracked file.

Keep physical device results separate from source checks and from emulator
results. State what a check did not establish.

## Configure a local source

The tracked manifest records store names, remotes, and environment variable
names. It holds no local location.

- To bind a store on one machine, set its variable. For example, set
  `QCOM_VP_REF_GOBBL` to the directory of the `gobbl` checkout.
- To keep a private binding out of the repository, pass `--manifest` with a
  private manifest. A relative store `root` in that manifest starts at the
  directory that contains the manifest.
- A variable takes priority over a `root` value.

Use absolute directories for environment variable bindings.

The tool reads no other configuration. It does not merge manifests and it
does not search for checkouts by itself.
For project references, copy the complete registry before adding private `root` values.
An alternative manifest replaces the registry. It does not only override locations.

The scanner recognizes declared store names and supported pinned remote forms.
It does not discover arbitrary citation formats or guarantee that every source mention is complete.

## Optional and private sources

Some stores exist only on one workstation. The manifest keeps their
identities and marks the expected contents. A missing local source is a
correct state of such a store. It is not evidence that the source is wrong.

A citation to a remote without a matching store entry counts as `remote_only`.
The checker does not contact that remote.
A declared store without a local binding remains unconfigured, even when it has a remote URL.

A bare historical revision ID refers to the private research archive unless the text names a repository.
Fresh public history may not contain those revisions.
Identify the source repository before constructing a public permalink.

## Interpret the result

The check prints one summary line and one line for each unresolved reference.

- `REFERENCES files= resolved= remote_only= unresolved=` is the summary. A
  non-zero `unresolved` count also gives a non-zero exit status.
- `resolved`: the local source holds the cited object or the cited digest.
- `remote_only`: the citation names a remote without a matching manifest store.
  The checker did not contact the remote.
- `unconfigured`: no variable and no `root` select a local source. The error
  names the variable to set.
- `absent`: the configured directory does not exist. The error names the
  variable and the `--manifest` option.
- `mismatch`: a local file has a different digest.
  A missing git commit or path instead reports an absent object.

Each failure prints `REFERENCE_UNRESOLVED <file>:<line> <citation>: <reason>`
before the summary. Do not read a non-zero status as a missing source only.
Read the reason text, because a mismatch or an absent object is a different
defect.

A reference path is relative to the store identity. It is not a workstation
path. Change the variable or the private `root`. Do not add a machine path to
a tracked document.
