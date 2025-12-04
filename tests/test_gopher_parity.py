import argparse
import subprocess
import sys
from typing import Tuple, Dict

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


def run_cpp(binary: str, text: str) -> Tuple[bool, str]:
    proc = subprocess.run(
        [binary],
        input=text,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"Binary {binary} failed with code {proc.returncode}")

    output = proc.stdout.strip()
    if not output:
        raise RuntimeError("No output from binary")
    parts = output.split("\t", 1)
    keep = parts[0].strip().lower() == "keep"
    reason = parts[1] if len(parts) > 1 else ""
    return keep, reason


def build_test_cases() -> Dict[str, str]:
    base_sentence = "the quick brown fox jumps over the lazy dog"
    long_clean = " ".join([base_sentence] * 7)  # 63 words

    hashes = " ".join(["content"] * 60 + ["#"] * 10)

    bullet_lines = "\n".join(["- bullet text " + base_sentence] * 12)  # 12 lines => 108 words

    ellipsis_lines = "\n".join(
        [
            f"{base_sentence}...",
            f"{base_sentence}...",
            f"{base_sentence}...",
            f"{base_sentence}...",
            f"{base_sentence}",
            f"{base_sentence}",
            f"{base_sentence}",
            f"{base_sentence}",
            f"{base_sentence}",
            f"{base_sentence}",
        ]
    )

    non_alpha = " ".join(["123"] * 40 + ["word"] * 10 + ["the"] * 10)

    ellipsis_symbol_ratio = " ".join(["alpha"] * 60 + ["..."] * 10)

    missing_stop_words = " ".join(["alpha"] * 60)

    return {
        "passes": long_clean,
        "short_doc": "hello world",
        "too_many_hashes": hashes,
        "too_many_bullets": bullet_lines,
        "too_many_end_ellipsis": ellipsis_lines,
        "low_alpha_ratio": non_alpha,
        "too_many_ellipsis_tokens": ellipsis_symbol_ratio,
        "missing_stop_words": missing_stop_words,
    }


def main():
    parser = argparse.ArgumentParser(description="Check C++ GopherQualityFilter parity with Python reference.")
    parser.add_argument("--binary", default="./build/gopher_filter_cli", help="Path to gopher_filter_cli binary")
    args = parser.parse_args()

    cases = build_test_cases()
    failures = []

    for name, text in cases.items():
        py_keep, py_reason = gopher_quality_py(text)
        cpp_keep, cpp_reason = run_cpp(args.binary, text)
        if (py_keep, py_reason) != (cpp_keep, cpp_reason):
            failures.append(
                {
                    "case": name,
                    "python": {"keep": py_keep, "reason": py_reason},
                    "cpp": {"keep": cpp_keep, "reason": cpp_reason},
                }
            )

    if failures:
        print("Parity test failures detected:")
        for f in failures:
            print(f)
        sys.exit(1)

    print("Parity test passed: C++ filter matches Python reference on all cases.")


if __name__ == "__main__":
    main()

