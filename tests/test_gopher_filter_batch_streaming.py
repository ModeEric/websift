import argparse
import gzip
import json
import subprocess
import tempfile
from pathlib import Path


def make_input(path: Path, texts):
    with gzip.open(path, "wt") as f:
        for idx, text in enumerate(texts):
            f.write(json.dumps({"id": f"id{idx}", "text": text}, separators=(",", ":")))
            f.write("\n")


def run_batch(binary: Path, input_path: Path, threads: int | None):
    cmd = [str(binary), str(input_path)]
    if threads is not None:
        cmd += ["--threads", str(threads)]
    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return json.loads(res.stdout)


def test_threaded_matches_single(binary: Path):
    texts = [
        "the quick brown fox jumps over the lazy dog.",
        "hello world # # #",
        "alpha beta gamma delta epsilon",
        "......",
        "bullet\n- list item one\n- list item two\n- list item three",
        "short",
        "the quick brown fox jumps over the lazy dog. " * 3,
    ]

    with tempfile.TemporaryDirectory() as tmpdir:
        input_path = Path(tmpdir) / "texts.jsonl.gz"
        make_input(input_path, texts)

        single = run_batch(binary, input_path, threads=None)
        multi = run_batch(binary, input_path, threads=4)

    assert single["docs"] == multi["docs"] == len(texts)
    assert single["kept"] == multi["kept"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="./build-local/gopher_filter_batch", type=Path)
    args = parser.parse_args()
    test_threaded_matches_single(args.binary)
    print("ok")


if __name__ == "__main__":
    main()
