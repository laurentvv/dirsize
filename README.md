# dirsize

Cross-platform command-line tool that scans a directory tree and produces a CSV
report of every first-level subdirectory, aggregated **recursively** (all
nesting levels included):

| Column | Description |
|---|---|
| `nom rep` | Subdirectory name |
| `taille` | Total size of all files, in bytes |
| `nombre de fichiers` | File count (recursive) |
| `date du fichier le plus ancien` | Oldest file modification date |
| `date du fichier le plus récent` | Newest file modification date |

Rows are sorted by size (largest first) and a `TOTAL` row is appended at the
end, including files sitting directly in the scanned root.

Single C++17 source file, no dependencies beyond the standard library
(`std::filesystem`). Builds and runs identically on Windows and Linux.

## Usage

```
dirsize <directory> <output.csv>
```

Example:

```
$ ./dirsize /srv/projects report.csv
  website : 312 fichier(s), 45678901 octets
  backups : 1 fichier(s), 32213321244 octets
  ...

3 sous-repertoires, 645787 fichiers, 89622220309 octets au total
CSV ecrit dans report.csv
```

Resulting CSV (semicolon-separated, opens directly in Excel — including French
locales):

```csv
"nom rep";"taille";"nombre de fichiers";"date du fichier le plus ancien";"date du fichier le plus recent"
"backups";32213321244;1;2021-04-26 15:45:50;2026-10-14 23:19:00
"website";45678901;312;2019-06-01 12:00:00;2026-10-14 19:38:41
"empty-dir";0;0;;
"TOTAL (projects)";3225899...;313;2019-06-01 12:00:00;2026-10-14 23:19:00
```

## Building

| Platform | Command |
|---|---|
| Linux / WSL (GCC ≥ 10) | `g++ -std=c++20 -O2 dirsize.cpp -o dirsize` |
| Old GCC (CentOS/RHEL 8, GCC 8–9) | `g++ -std=c++17 -O2 dirsize.cpp -o dirsize -lstdc++fs` |
| **Very old GCC (CentOS/RHEL 7, GCC 4.8.5)** | `g++ -std=c++11 -O2 dirsize-legacy.cpp -o dirsize` |
| Windows (MinGW / MSYS2) | `g++ -std=c++20 -O2 -static dirsize.cpp -o dirsize.exe` |
| Windows (MSVC) | `cl /std:c++20 /EHsc dirsize.cpp` |

For locked-down production machines that cannot install devtoolset or any
newer compiler, `dirsize-legacy.cpp` is a C++11 / pure-POSIX variant of the
tool (`dirent.h` + `lstat`, no `std::filesystem`, no external dependency).
It produces byte-identical CSV output to `dirsize.cpp` on Linux (validated on
a full `/usr` tree plus symlink / bind-mount / pre-1970 fixtures), keeps the
`(device, inode)` bind-mount cycle protection, and counts symlinks the same
way.

Notes:

- `-static` (MinGW) produces a standalone `.exe` with no runtime DLL
  dependency. Without it, the binary requires `libstdc++-6.dll` on `PATH`.
- `-lstdc++fs` is **required** on GCC 8/9 (CentOS 8, RHEL 8) and harmless on
  newer compilers.
- Both C++17 and C++20 modes produce identical, second-exact dates: the code
  uses `chrono::clock_cast` when available and an equally exact fallback
  otherwise.

## Behavior details

- Dates are file **last-modification** times, displayed in local time as
  `YYYY-MM-DD HH:MM:SS`.
- Symbolic links, NTFS junctions and other reparse points are **never followed
  and never counted** — no loops, no double counting. On Windows this is
  enforced at the enumeration level (`FILE_ATTRIBUTE_REPARSE_POINT`), matching
  the default behavior of `Get-ChildItem -Recurse`. Outputs were verified
  byte-exact against PowerShell on junction-heavy trees (pnpm `node_modules`
  layouts, junctioned top-level directories).
- Hard-linked files are counted once per directory entry (same as
  `Get-ChildItem`).
- On Linux, already-visited directories are tracked by `(device, inode)`, so
  bind-mount cycles cannot hang the scan and a tree bind-mounted in two places
  is counted once (same approach as `find`/`du`). Symlinks are skipped via
  `is_symlink()` there, which behaves correctly on POSIX.
- File modification dates before 1970 are supported on both platforms
  (signed FILETIME math + Win32 `SystemTimeToTzSpecificLocalTime` on Windows,
  where the UCRT `localtime` rejects negative `time_t`).
- Unreadable directories produce a warning on stderr and are skipped; the scan
  continues.
- Empty subdirectories are listed with zero size/count and empty date fields.
- CSV is written as UTF-8, with a BOM on Windows so Excel renders accented
  names correctly (no BOM on Linux). Fields containing `;` or `"` are quoted
  with standard `""` escaping.
- Exit code is `0` on success, `1` on usage error, invalid input directory, or
  unwritable output file.

## Performance

On Windows the scan is native (`FindFirstFileExW` with `FIND_FIRST_EX_LARGE_FETCH`):
each enumeration buffer already carries size, timestamps and attributes, so no
per-file `stat()` call is ever issued.

Measured on a 90 GB / 643,000-file tree (46 top-level directories, warm cache):

| Tool | Time |
|---|---|
| **dirsize** (native Windows scan) | **~2.9 s** |
| PowerShell `Get-ChildItem -Recurse` + date aggregation | ~23.6 s |
| `std::filesystem`-only build (initial version) | ~60 s |

On Linux, the scan reads each file's size and mtime from a single `stat()`
call (down from two): a 41,000-file `/usr` tree scans in ~1.9 s warm, roughly
1.5–2× faster and far more consistent than the previous `std::filesystem`
path. Cold-cache runs are disk-bound on both platforms.

Regression tests live in [`tests/`](tests/): `run-windows-tests.ps1` covers
junctions, exact boundary dates and pre-1970 dates on both Windows code paths
(first-level `GetFileAttributesExW` and native recursive scan); the Linux
suite (`run-linux-tests.sh`) covers symlinks, pre-1970 dates, bind mounts and
C++17/C++20 output identity.

Note for MinGW users: `std::chrono::clock_cast` in libstdc++ returns `-1`
for file times before 1970 (while `fs::last_write_time` is correct), which is
why all Windows date handling uses exact integer FILETIME math instead of
`<chrono>`.

## License

[MIT](LICENSE)
