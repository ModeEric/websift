import subprocess
import time
import os
import argparse
import resource

def parse_profiling_stats(output):
    stats = {}
    in_stats = False
    for line in output.splitlines():
        if "--- Profiling Stats ---" in line:
            in_stats = True
            continue
        if not in_stats:
            continue
        if line.startswith("Name"): continue
        if line.startswith("-------"): continue
        if not line.strip(): continue
        
        # Parse line: Name (25) Total (15) Calls (10) Avg (15)
        parts = line.split()
        if len(parts) >= 4:
            name = parts[0]
            try:
                total_ms = float(parts[1])
                calls = int(parts[2])
                avg_ms = float(parts[3])
                stats[name] = {
                    "total_ms": total_ms,
                    "calls": calls,
                    "avg_ms": avg_ms
                }
            except ValueError:
                continue
    return stats

def run_benchmark(binary, input_file, limit=None):
    cmd = [binary, input_file]
    if limit:
        cmd.extend(["--limit", str(limit)])
    
    print(f"Benchmarking: {' '.join(cmd)}")
    
    start_time = time.time()
    
    # Run the command
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    end_time = time.time()
    
    if result.returncode != 0:
        print("Error running benchmark:")
        print(result.stderr)
        return None

    # Get resource usage of children
    # Note: This includes ALL children of the python process since start.
    # If we run multiple things, this might be inaccurate. 
    # But in this script we only run one thing.
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    
    # max_rss is in kilobytes on Linux, bytes on macOS.
    # To be safe, we can try to guess or check OS.
    # On macOS (Darwin), it's bytes.
    import platform
    max_rss = usage.ru_maxrss
    if platform.system() == "Darwin":
        max_rss_mb = max_rss / 1024 / 1024
    else:
        # Linux is usually KB
        max_rss_mb = max_rss / 1024

    # Parse stdout for app metrics
    output = result.stdout
    stats = parse_profiling_stats(output)
    
    # Parse app reported speed
    docs_sec = 0
    mb_sec = 0
    for line in output.splitlines():
        if "Docs/sec:" in line:
            docs_sec = float(line.split(":")[1].strip())
        if "MB/sec:" in line:
            mb_sec = float(line.split(":")[1].strip())

    return {
        "wall_time": end_time - start_time,
        "user_time": usage.ru_utime,
        "sys_time": usage.ru_stime,
        "max_rss_mb": max_rss_mb,
        "docs_sec": docs_sec,
        "mb_sec": mb_sec,
        "profile": stats
    }

def print_report(metrics):
    if not metrics: return
    print("\n" + "="*65)
    print(f"{'BENCHMARK REPORT':^65}")
    print("="*65)
    print(f"Total Wall Time: {metrics['wall_time']:>10.2f} s")
    print(f"User Time:       {metrics['user_time']:>10.2f} s")
    print(f"System Time:     {metrics['sys_time']:>10.2f} s")
    print(f"Max Memory (RSS):{metrics['max_rss_mb']:>10.2f} MB")
    print("-" * 65)
    print(f"Throughput:      {metrics['docs_sec']:>10.2f} docs/s")
    print(f"                 {metrics['mb_sec']:>10.2f} MB/s")
    
    print("\nComponent Breakdown:")
    print("-" * 65)
    print(f"{'Component':<25} {'Total (ms)':>12} {'% Time':>8} {'Avg (ms)':>10} {'Calls':>8}")
    print("-" * 65)
    
    profile = metrics['profile']
    # Use Wall Time as base? Or sum of components?
    # Sum of components is safer for relative comparison.
    # But extraction + filters != total time (there is IO overhead).
    # Let's use wall time * 1000 for %.
    
    total_wall_ms = metrics['wall_time'] * 1000
    
    sorted_items = sorted(profile.items(), key=lambda x: x[1]['total_ms'], reverse=True)
    
    for name, data in sorted_items:
        pct = (data['total_ms'] / total_wall_ms * 100) if total_wall_ms > 0 else 0
        print(f"{name:<25} {data['total_ms']:>12.2f} {pct:>8.1f}% {data['avg_ms']:>10.4f} {data['calls']:>8}")
    print("-" * 65)
    
    # Calculate unprofiled time (IO, parsing, overhead)
    sum_profiled = sum(d['total_ms'] for d in profile.values())
    overhead = total_wall_ms - sum_profiled
    overhead_pct = (overhead / total_wall_ms * 100) if total_wall_ms > 0 else 0
    print(f"{'IO/Parsing/Overhead':<25} {overhead:>12.2f} {overhead_pct:>8.1f}% {'-':>10} {'-':>8}")
    print("-" * 65)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="./build/websift")
    parser.add_argument("--input", required=True)
    parser.add_argument("--limit", type=int, default=None)
    args = parser.parse_args()
    
    metrics = run_benchmark(args.binary, args.input, args.limit)
    print_report(metrics)
