# Prism Launcher support disabled

Prism Launcher integration has been intentionally disabled in this repository.
The Prism-specific macOS wrapper scripts are no longer functional and should
not be used.

The following files are deprecated and retained only for historical reference:

- `scripts/macos-prism-wrapper.sh`
- `scripts/macos-prism-enable-wrapper.sh`
- `scripts/macos-prism-disable-wrapper.sh`
- `scripts/macos-prism-set-overlay-mode.sh`

Do not run these scripts against Prism Launcher instances. They will print a
message indicating that Prism integration is disabled and then exit.
