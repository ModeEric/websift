# Websift

High-speed WARC processing with C++ filters and parity-tested logic against the reference Python implementation.

## Binaries

- `websift`: full pipeline (WARC read → HTML extraction → filters → CSV).
- `gopher_filter_cli`: single-text filter over stdin; prints `keep<TAB>reason`.
- `gopher_filter_batch`: filter-only benchmark/processor over JSONL/JSONL.gz (`{"id","text"}` per line).
- `extract_texts`: fast extractor from WARC/warc.gz to JSONL (`{"id","text"}`).

## Build

Release + LTO recommended:
```
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build-release -j
```

## Usage

Extract texts from WARC to JSONL:
```
./build-release/extract_texts CC-MAIN-20251119093413-20251119123413-00999.warc.gz --limit 500 --output texts.jsonl
gzip -f texts.jsonl
```

Filter-only benchmark on pre-extracted texts (C++):
```
./build-release/gopher_filter_batch texts.jsonl.gz --limit 500
```

Filter-only benchmark on pre-extracted texts (Python reference):
```
python3 scripts/benchmark_python_gopher.py --input-jsonl texts.jsonl.gz --limit 500
```

Full pipeline benchmark:
```
python3 scripts/benchmark.py --binary ./build-release/websift --input CC-MAIN-20251119093413-20251119123413-00999.warc.gz --limit 500
```

Accuracy check (C++ vs Python parity cases):
```
python3 tests/test_gopher_parity.py --binary ./build-release/gopher_filter_cli
```

## Current performance snapshot (Release, limit=500, same sample)

- Filter-only, C++: ~1425 docs/s, ~45.7 MB/s (`gopher_filter_batch` on `texts.jsonl.gz`)
- Filter-only, Python: ~429 docs/s, ~11.4 MB/s (same input)
- Full pipeline (WARC → extract → filters): ~227 docs/s, ~22 MB/s (`websift`)

## Notes

- All filters pass parity tests with the reference Python implementation.
- Memory footprint stays ~21 MB RSS in the measured runs.