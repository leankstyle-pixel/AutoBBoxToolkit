# AutoBBoxToolkit

AutoBBoxToolkit is a recovered and reorganized Creo TOOLKIT plug-in workspace. It provides Creo automation commands for model bounding-box parameters, volume data, drawing view helpers, BOM/parameter tooling, family table workflows, quick rename, sheet-metal flattening helpers, and related productivity features.

## Repository status

This workspace was rebuilt from runtime artifacts and then reorganized into a maintainable source layout. The current source tree is intended to be the canonical working tree for future development.

See also:

- `README_RECOVERY.md` for recovery background
- `docs/REBUILD_SOURCE_STATUS.md` for the current modularization status

## Layout

- `src/main` - plug-in bootstrap, command registration, and command dispatch
- `src/application` - feature workflows and business logic
- `src/creo` - Creo TOOLKIT-facing helper wrappers
- `src/ui` - native Creo dialog/controller logic
- `src/common` - logging, path, string, and shared utility helpers
- `include/autobbox` - public/internal project headers
- `resource` - source `.res` dialog resource files
- `ribbon` - source ribbon definition
- `text/resource` - ribbon icon/image resources
- `scripts` - local build and index helper scripts

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

## Optional AFX library search hook

The AFX "Select From Library" dialog can be extended with a search input by
patching the installed AFX dialog resource. Typing filters/selects matching
library items, and Enter or the Next button cycles through additional matches:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\patch_afx_library_search.ps1
```

By default the script patches the standalone AFX installation at
`D:\Program Files\buw\AFX 10.0.8.0\text\resource`. Pass `-AfxResourceRoot` only
if a different AFX resource directory should be patched.

The script backs up the original files as `*.autobbox-search-backup`. To undo
the resource change:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\restore_afx_library_search.ps1
```

## DWG export font handling

DWG post-processing reads the current drawing setup/detail options loaded from
the user's `drawing.dtl`, preferring `default_annotation_font` and then
`default_font`. When Creo's internal stroke font values such as `font` or
`font.ndx` are encountered, AutoBBox writes DWG text styles with
`ChangFangSong.ttf` instead so exported DWGs keep a consistent long Fangsong
font.

## Notes for maintainers

- Do not commit local Creo SDK headers, libraries, generated install indexes, DLLs, PDBs, or runtime deployment mirrors.
- Keep source resources in `resource/`, `ribbon/`, `text/resource/`, and `autobbox_msg.txt`; deployment mirrors are generated/local.
- Project-local evidence/index folders under `.autobbox/` are local development aids and are intentionally excluded from public source control.

## License

This project is licensed under the MIT License. See LICENSE for details.

## Disclaimer

AutoBBoxToolkit is an independent community project. It is not affiliated with, endorsed by, or sponsored by PTC. Creo and Creo TOOLKIT are trademarks or registered trademarks of their respective owners.
