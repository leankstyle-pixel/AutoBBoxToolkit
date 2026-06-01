# AutoBBoxToolkit

AutoBBoxToolkit is a recovered and reorganized Creo TOOLKIT plug-in workspace. It provides Creo automation commands for model bounding-box parameters, volume data, drawing view helpers, BOM/parameter tooling, family table workflows, quick rename, sheet-metal flattening helpers, and related productivity features.

## Repository status

This workspace was rebuilt from runtime artifacts and then reorganized into a maintainable source layout. The current source tree is intended to be the canonical working tree for future development.

See also:

- `README_RECOVERY.md` for recovery background
- `docs/REBUILD_SOURCE_STATUS.md` for the current modularization status

## Layout

- `src/main` 鈥?plug-in bootstrap, command registration, and command dispatch
- `src/application` 鈥?feature workflows and business logic
- `src/creo` 鈥?Creo TOOLKIT-facing helper wrappers
- `src/ui` 鈥?native Creo dialog/controller logic
- `src/common` 鈥?logging, path, string, and shared utility helpers
- `include/autobbox` 鈥?public/internal project headers
- `resource` 鈥?source `.res` dialog resource files
- `ribbon` 鈥?source ribbon definition
- `text/resource` 鈥?ribbon icon/image resources
- `scripts` 鈥?local build and index helper scripts

Generated runtime outputs such as `build/`, `deploy/`, `runtime/`, and `package/` are intentionally ignored by git.

## Requirements

- Windows
- CMake 3.20 or newer
- Visual Studio C++ toolchain compatible with the installed Creo TOOLKIT libraries
- Creo 10.0.8.0 with TOOLKIT installed

The default CMake configuration expects Creo TOOLKIT under:

```text
D:/Program Files/PTC/Creo 10.0.8.0/Common Files/protoolkit
```

Override `CREO_TOOLKIT_ROOT` when configuring if your installation is elsewhere.

## Build

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The project-specific build helper is:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_autobbox.ps1
```

## Notes for maintainers

- Do not commit local Creo SDK headers, libraries, generated install indexes, DLLs, PDBs, or runtime deployment mirrors.
- Keep source resources in `resource/`, `ribbon/`, `text/resource/`, and `autobbox_msg.txt`; deployment mirrors are generated/local.
- Project-local evidence/index folders under `.autobbox/` are local development aids and are intentionally excluded from public source control.

## License

This project is licensed under the MIT License. See LICENSE for details.

## Disclaimer

AutoBBoxToolkit is an independent community project. It is not affiliated with, endorsed by, or sponsored by PTC. Creo and Creo TOOLKIT are trademarks or registered trademarks of their respective owners.

