# Contributing

Issues and pull requests are welcome.

- **Bugs**: please include your OS, host and version, plugin format, and steps to reproduce. A preset file
  (`.rvpreset`) helps.
- **Pull requests**: keep them focused, and make sure `rv_render` and `rv_plugin_tests` pass (CI runs both on all
  three platforms). DSP changes should come with a check in `tests/render_test.cpp` that measures the behaviour,
  not just that it runs.
- **Real-time safety**: nothing in `processBlock` or `Source/dsp/` may allocate, lock or wait.
- **Licence**: contributions are accepted under the project's licence, AGPL-3.0-or-later. Add
  `// SPDX-License-Identifier: AGPL-3.0-or-later` to new source files.
