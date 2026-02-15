# projectM Integration Policy

This repository is a standalone Qt6 application that links to the upstream projectM shared
library (libprojectM-4). It does not vendor or fork projectM source code.

## Policy

- Use the official projectM C API and ABI for all rendering/audio integration.
- Do not copy or embed projectM source code in this repository.
- Prefer system-provided projectM packages during development.
- AppImage releases bundle the shared library for portability, but the library is unmodified.
- If libprojectM changes are required, propose them upstream rather than in this repo.

## Compatibility

- The app targets the projectM 4.x C API. Rebuilds against newer 4.x releases should remain
  compatible as long as the upstream ABI contract holds.
- When upstream publishes API changes in 4.x, update integration in this repo and document
  the required minimum version in README.
