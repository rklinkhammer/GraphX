# GOTCHA Input Manifest Schema

This document defines the manifest used by the future GOTCHA importer to impose deterministic input file ordering. The manifest is an ordering contract only: GraphX does not use MATLAB, does not parse MAT contents in this step, and does not add HDF5, MAT reader, lite writer, CRSD writer, CLI, or Python tooling here.

## Schema

Schema name: `graphx.gotcha.input_manifest.v1`

```json
{
  "schema": "graphx.gotcha.input_manifest.v1",
  "files": [
    { "path": "pass1_pulse001.mat" },
    { "path": "pass1_pulse002.mat" }
  ]
}
```

## Rules

- `schema` is required and must equal `graphx.gotcha.input_manifest.v1`.
- `files` is required and must be a non-empty array.
- Each `files[]` entry is an object with a non-empty string `path`.
- Paths are relative to the input directory.
- Absolute paths and parent traversal are invalid.
- Duplicate paths are invalid.
- Manifest order is authoritative.
- Lexical mode ignores the manifest and sorts matching `.mat` files by filename.
- Empty lexical input directories are invalid.

The files named by the manifest remain opaque input files. Later PRs may inspect or parse MAT files, but this PR only produces a deterministic ordered path list.
