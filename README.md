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
| Windows (MinGW / MSYS2) | `g++ -std=c++20 -O2 -static dirsize.cpp -o dirsize.exe` |
| Windows (MSVC) | `cl /std:c++20 /EHsc dirsize.cpp` |

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
- Symbolic links and junctions are skipped (no loops, no double counting).
- Unreadable directories produce a warning on stderr and are skipped; the scan
  continues.
- Empty subdirectories are listed with zero size/count and empty date fields.
- CSV is written as UTF-8, with a BOM on Windows so Excel renders accented
  names correctly (no BOM on Linux). Fields containing `;` or `"` are quoted
  with standard `""` escaping.
- Exit code is `0` on success, `1` on usage error, invalid input directory, or
  unwritable output file.

## Performance

A ~90 GB tree with 645,000 files across 46 subdirectories scans in about
2 minutes on a consumer machine — disk-bound, not CPU-bound.

## License

[MIT](LICENSE)
