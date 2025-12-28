import argparse
import gzip
import subprocess
import tempfile
from pathlib import Path


def make_warc(path: Path):
    records = []
    payload1 = "HTTP/1.1 200 OK\r\n\r\nThe quick brown fox jumps over the lazy dog.\n"
    payload2 = "HTTP/1.1 200 OK\r\n\r\n \n"  # empty-ish content to trigger drop
    for idx, payload in enumerate([payload1, payload2], start=1):
        content_length = len(payload)
        record = (
            "WARC/1.0\r\n"
            "WARC-Type: response\r\n"
            f"WARC-Target-URI: http://example.com/{idx}\r\n"
            f"WARC-Record-ID: <urn:uuid:{idx}>\r\n"
            f"Content-Length: {content_length}\r\n"
            "\r\n"
            f"{payload}"
            "\r\n\r\n"
        )
        records.append(record)

    data = "".join(records).encode("utf-8")
    with gzip.open(path, "wb") as f:
        f.write(data)


def run_websift(binary: Path, warc_path: Path, threads: int | None):
    cmd = [str(binary), str(warc_path), "--limit", "2"]
    if threads is not None:
        cmd += ["--threads", str(threads)]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return proc.stdout


def parse_counts(output: str):
    counts = {}
    for line in output.splitlines():
        if ":" not in line:
            continue
        key, val = line.split(":", 1)
        key = key.strip().lower()
        val = val.strip()
        if key in {"total docs", "kept docs", "dropped docs"}:
            counts[key] = int(float(val))
    return counts


def test_threaded_matches_single(binary: Path):
    with tempfile.TemporaryDirectory() as tmpdir:
        warc_path = Path(tmpdir) / "sample.warc.gz"
        make_warc(warc_path)

        single_out = run_websift(binary, warc_path, threads=None)
        multi_out = run_websift(binary, warc_path, threads=4)

    single = parse_counts(single_out)
    multi = parse_counts(multi_out)
    assert single == multi
    assert single.get("total docs") == 2
    assert single.get("kept docs") == 0
    assert single.get("dropped docs") == 2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="./build-local/websift", type=Path)
    args = parser.parse_args()
    test_threaded_matches_single(args.binary)
    print("ok")


if __name__ == "__main__":
    main()
