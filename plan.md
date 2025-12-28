# Performance roadmap toward Datatrove-style FineWeb processing

Ordered, actionable steps to reach Datatrove-level throughput and functionality (including the FineWeb pipeline stages) with clear priorities and expected impact.

1) **Streaming worker pool in `gopher_filter_batch` (highest priority)**
   - Replace the current “buffer then fan-out” parallel path with a bounded queue feeding persistent worker threads.
   - Overlaps gzip read + JSON parse + filtering, cuts per-run thread setup overhead, and avoids holding all texts in memory.
   - Expected impact: recover single-thread latency, and unlock real multi-core scaling for large inputs without the current small-batch slowdown.

2) **Propagate streaming concurrency to `websift` end-to-end**
   - Apply the producer/worker design to WARC read → HTTP/body strip → HTML strip → filters, with bounded queues between stages.
   - Add CLI controls for worker counts and queue depths to tune for hardware.
   - Expected impact: raise full-pipeline docs/sec toward filter-only rates; keep memory bounded while overlapping I/O and CPU.

3) **Implement FineWeb/DATATROVE-equivalent pipeline stages**
   - Build a `websift` pipeline driver that mirrors Datatrove steps: WARC reader, URL filter, HTML extractor (Trafilatura-like fidelity), language filter, Gopher repetition and quality filters, C4 quality (with configurable `filter_no_terminal_punct`), FineWeb quality filter, token counting, and JSONL writers for both kept and exclusion outputs.
   - Support exclusion outputs per stage (mirroring `JsonlWriter` per filter) and directory layout compatible with the FineWeb workflow.
   - Expected impact: feature parity with the FineWeb processing flow inside this repo, enabling end-to-end dataset creation without Datatrove.

4) **Add MinHash-based deduplication stages**
   - Implement Minhash signature generation, bucket aggregation, clustering, and ID filtering (analogous to `MinhashDedupSignature`, `MinhashDedupBuckets`, `MinhashDedupCluster`, `MinhashDedupFilter`) with configurable n-grams, buckets, and hash precision.
   - Provide CLI/driver scripts to run the four stages sequentially or as discrete jobs; include token counting and PII-formatting hooks before final JSONL output.
   - Expected impact: dataset-level dedup matching the Datatrove FineWeb flow, reducing duplicates before downstream training.

5) **SIMD-friendly tokenization and line scanning**
   - Replace scalar whitespace/punctuation scans with SIMD-friendly routines (SSE/AVX byte-class tables) and shared scratch buffers.
   - Expected impact: lower per-document CPU cost in filters, boosting single-thread throughput and improving multi-core efficiency.

6) **Configurable chunk sizing, queue depths, and profiling hooks**
   - Expose CLI knobs for queue depth/chunk size and add lightweight timing counters around each stage (I/O, parsing, filtering, dedup).
   - Expected impact: faster tuning across hardware and datasets; makes regressions visible during benchmarking and large-batch runs.

7) **Expanded parity/robustness tests for filters and dedup**
   - Broaden fixtures to cover long lines, mixed encodings, edge punctuation, and dedup edge cases; add regression tests for Minhash stages.
   - Expected impact: confidence that performance changes preserve behavior relative to the Python reference, FineWeb expectations, and real-world edge cases.
