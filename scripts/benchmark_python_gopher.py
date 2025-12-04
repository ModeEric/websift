import argparse
import time
import gzip
import json
import sys
import os
from typing import List, Tuple

# Reference Python filter implementation (same as test parity)
STOP_WORDS = {"the", "be", "to", "of", "and", "that", "have", "with"}
PUNCTUATION_SET = set(r'!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~')
BULLET = "\u2022"
UNICODE_ELLIPSIS = "\u2026"


def gopher_quality_py(
    text: str,
    min_doc_words: int = 50,
    max_doc_words: int = 100000,
    min_avg_word_length: int = 3,
    max_avg_word_length: int = 10,
    max_symbol_word_ratio: float = 0.1,
    max_bullet_lines_ratio: float = 0.9,
    max_ellipsis_lines_ratio: float = 0.3,
    max_non_alpha_words_ratio: float = 0.8,
    min_stop_words: int = 2,
    stop_words=STOP_WORDS,
) -> Tuple[bool, str]:
    words = text.split()
    n_words = len(words)

    non_symbol_words = [w for w in words if any(ch not in PUNCTUATION_SET for ch in w)]
    n_non_symbol_words = len(non_symbol_words)

    if min_doc_words and n_non_symbol_words < min_doc_words:
        return False, "gopher_short_doc"
    if max_doc_words and n_non_symbol_words > max_doc_words:
        return False, "gopher_long_doc"

    avg_len = (sum(len(w) for w in non_symbol_words) / n_non_symbol_words) if non_symbol_words else 0
    if min_avg_word_length and avg_len < min_avg_word_length:
        return False, "gopher_below_avg_threshold"
    if max_avg_word_length and avg_len > max_avg_word_length:
        return False, "gopher_above_avg_threshold"

    if max_symbol_word_ratio:
        if n_words == 0:
            return False, "gopher_short_doc"
        if text.count("#") / n_words > max_symbol_word_ratio:
            return False, "gopher_too_many_hashes"
        if (text.count("...") + text.count(UNICODE_ELLIPSIS)) / n_words > max_symbol_word_ratio:
            return False, "gopher_too_many_ellipsis"

    lines = text.splitlines()
    if not lines:
        lines = [text]

    if (
        max_bullet_lines_ratio
        and sum(line.lstrip().startswith(BULLET) or line.lstrip().startswith("-") for line in lines) / len(lines)
        > max_bullet_lines_ratio
    ):
        return False, "gopher_too_many_bullets"

    if (
        max_ellipsis_lines_ratio
        and sum(line.rstrip().endswith("...") or line.rstrip().endswith(UNICODE_ELLIPSIS) for line in lines) / len(lines)
        > max_ellipsis_lines_ratio
    ):
        return False, "gopher_too_many_end_ellipsis"

    if (
        max_non_alpha_words_ratio
        and sum(any(c.isalpha() for c in w) for w in words) / n_words < max_non_alpha_words_ratio
    ):
        return False, "gopher_below_alpha_threshold"

    if min_stop_words and sum(w in stop_words for w in words) < min_stop_words:
        return False, "gopher_enough_stop_words"

    return True, ""


def iter_texts(path: str, limit: int) -> List[str]:
    """
    Minimal reader: assumes JSON lines produced by the C++ pipeline as csv-output is not convenient here.
    For parity, we can reuse the extraction already done by the C++ binary to produce a JSON lines file:
    {
        "id": "<record_id>",
        "text": "<extracted_text>"
    }
    """
    texts = []
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if not line.strip():
                continue
            try:
                obj = json.loads(line)
                text = obj.get("text", "")
                if text:
                    texts.append(text)
                    if len(texts) >= limit:
                        break
            except Exception:
                continue
    return texts


def benchmark_py(texts: List[str]):
    start = time.time()
    bytes_total = 0
    for t in texts:
        gopher_quality_py(t)
        bytes_total += len(t.encode("utf-8", errors="ignore"))
    elapsed = time.time() - start
    docs_sec = len(texts) / elapsed if elapsed > 0 else 0
    mb_sec = (bytes_total / 1024 / 1024) / elapsed if elapsed > 0 else 0
    return {
        "docs": len(texts),
        "elapsed": elapsed,
        "docs_sec": docs_sec,
        "mb_sec": mb_sec,
    }


def main():
    parser = argparse.ArgumentParser(description="Benchmark Python GopherQualityFilter on pre-extracted texts.")
    parser.add_argument("--input-jsonl", required=True, help="Path to JSONL (optionally gzipped) with {'id','text'}.")
    parser.add_argument("--limit", type=int, default=500)
    args = parser.parse_args()

    if not os.path.exists(args.input_jsonl):
        print(f"Input not found: {args.input_jsonl}")
        sys.exit(1)

    texts = iter_texts(args.input_jsonl, args.limit)
    if not texts:
        print("No texts loaded; ensure input JSONL.gz exists and has 'text' fields.")
        sys.exit(1)

    metrics = benchmark_py(texts)
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()

