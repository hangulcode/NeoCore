"""Paired, rotated timing of the eight-case benchmark suite.

Usage:
  python Tools/bench/measure.py [--rounds 5] [--label NAME] [--out DIR] [--bench FILE]
                                [--neoscript EXE|none] [--lua EXE|none] [--cpp EXE|none]
                                NAME=neocore.exe [NAME=neocore.exe ...]

Every NAME=exe is a NeoCore console; the first one is the build the ratios describe.
NeoScript, Lua and the C++ reference are added unless disabled with `none`. One warmup
round is followed by --rounds measured rounds; in every round each executable runs once,
in an order rotated by one position per round, so no program always follows the same
predecessor. Each process reports the best of its internal repetitions per case (five in
the checked-in sources); the result per case is the median of the per-round values, with
the per-round range kept for judging whether two builds differ. Checksums must agree
across every process or the run stops. The method, the rules around it and how to read
the output are in docs/Benchmarking.md.

Outputs under --out (default Build/bench-sessions/<label>): every process's stdout,
measurements.json (medians, ranges, checksums, executable hashes, dispatch offsets) and
summary.md with the table in the readme's format.
"""
import argparse
import datetime
import hashlib
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dispatch_offset import benchmark_offset  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
CASES = ['loop_sum', 'float_math', 'func_call', 'fib_recursive', 'array_rw', 'map_str', 'string_ops', 'particles']


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--rounds', type=int, default=5, help='measured rounds after the warmup (default 5)')
    parser.add_argument('--reps', type=int, default=5,
                        help='in-process repetitions per case for the NeoCore script (default 5, the checked-in value); '
                             'another value runs a patched copy and disables the references')
    parser.add_argument('--quick', action='store_true',
                        help='development run: three rounds, three in-process repetitions, NeoCore builds only')
    parser.add_argument('--label', default=datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    parser.add_argument('--out', help='session directory (default Build/bench-sessions/<label>)')
    parser.add_argument('--bench', default=str(ROOT / 'Samples/bench/bench.nc'), help='NeoCore benchmark script')
    parser.add_argument('--neoscript', default=os.environ.get('NEOCORE_BENCH_NEOSCRIPT',
                        str(ROOT.parent / 'NeoScript/Samples/console/x64/Release/console.exe')),
                        help='NeoScript console, or none; runs bench.ns from its own repository')
    parser.add_argument('--lua', default=os.environ.get('NEOCORE_BENCH_LUA', r'D:\util\lua-5.5_Win64_bin\lua55.exe'),
                        help='Lua 5.5 executable, or none')
    parser.add_argument('--cpp', default=os.environ.get('NEOCORE_BENCH_CPP', str(ROOT / 'Samples/bench/bench_cpp.exe')),
                        help='C++ reference built by Samples/bench/build_cpp.bat, or none')
    parser.add_argument('programs', nargs='+', metavar='NAME=EXE', help='NeoCore consoles to time')
    return parser.parse_args()


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    options = parse_arguments()
    if options.quick:
        options.rounds, options.reps = 3, 3
        options.neoscript = options.lua = options.cpp = 'none'
    out = Path(options.out) if options.out else ROOT / 'Build/bench-sessions' / options.label
    out.mkdir(parents=True, exist_ok=True)
    bench_dir = ROOT / 'Samples/bench'
    bench = Path(options.bench).resolve()
    if options.reps != 5:
        # 반복 횟수를 바꾼 사본은 저장소 밖(세션 폴더)에 둔다. 참조 구현은 5회 고정이라 함께 잴 수 없다.
        if any(option.lower() != 'none' for option in (options.neoscript, options.lua, options.cpp)):
            sys.exit('--reps other than 5 compares NeoCore builds only; pass --neoscript/--lua/--cpp none')
        text = bench.read_text(encoding='utf-8-sig')
        if text.count('int REPS = 5;') != 1:
            sys.exit('cannot find the REPS declaration in %s' % bench)
        bench = out / ('bench_reps%d.nc' % options.reps)
        bench.write_text(text.replace('int REPS = 5;', 'int REPS = %d;' % options.reps), encoding='utf-8-sig')

    programs = []  # (name, command, working directory, is NeoCore)
    for item in options.programs:
        name, exe = item.split('=', 1)
        exe = str(Path(exe).resolve())
        programs.append((name, [exe, '--file', str(bench)], bench_dir, True))
    if options.neoscript.lower() != 'none':
        console = Path(options.neoscript).resolve()
        ns_dir = console.parents[3] / 'bench'
        programs.append(('NeoScript', [str(console), '--file', str(ns_dir / 'bench.ns')], ns_dir, False))
    if options.lua.lower() != 'none':
        programs.append(('Lua', [str(Path(options.lua).resolve()), str(bench_dir / 'bench.lua')], bench_dir, False))
    if options.cpp.lower() != 'none':
        programs.append(('Cpp', [str(Path(options.cpp).resolve())], bench_dir, False))
    for name, command, _, _ in programs:
        if not Path(command[0]).is_file():
            sys.exit('%s executable not found: %s (pass none to skip a reference)' % (name, command[0]))

    data = {
        'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'label': options.label,
        'method': 'One warmup round plus %d rotated serial rounds over %d executables; each process reports the '
                  'best of %d internal repetitions per case; the table takes the median across the measured rounds.'
                  % (options.rounds, len(programs), options.reps),
        'reps': options.reps,
        'executables': {},
        'sources': {},
        'runs': [],
    }
    for name, command, _, is_neocore in programs:
        entry = {'path': command[0], 'sha256': sha256(command[0])}
        if is_neocore:
            entry['dispatch_offset'] = benchmark_offset(command[0])
        data['executables'][name] = entry
    for name, command, run_dir, _ in programs:
        script = command[-1] if len(command) > 1 else None
        if script and Path(script).is_file():
            data['sources'][Path(script).name] = sha256(script)

    expected = None
    for round_number in range(options.rounds + 1):
        shift = round_number % len(programs)
        order = programs[shift:] + programs[:shift]
        for name, command, run_dir, _ in order:
            result = subprocess.run(command, cwd=str(run_dir), capture_output=True, timeout=600)
            text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
            (out / ('round-%d-%s.txt' % (round_number, name))).write_text(text, encoding='utf-8')
            if result.returncode:
                sys.exit('%s failed with code %d\n%s' % (name, result.returncode, text))
            rows = {}
            for line in text.splitlines():
                fields = line.strip().split('|')
                if len(fields) == 3 and fields[0] in CASES:
                    if fields[0] in rows:
                        sys.exit('duplicate benchmark row from %s: %s' % (name, line))
                    rows[fields[0]] = {'microseconds': int(fields[1]), 'checksum': int(fields[2])}
            if set(rows) != set(CASES):
                sys.exit('missing benchmark rows from %s:\n%s' % (name, text))
            checksums = {case: rows[case]['checksum'] for case in CASES}
            if expected is None:
                expected = checksums
            elif checksums != expected:
                sys.exit('checksum mismatch in %s: %s != %s' % (name, checksums, expected))
            data['runs'].append({'round': round_number, 'warmup': round_number == 0, 'language': name, 'rows': rows})
            (out / 'measurements.json').write_text(json.dumps(data, indent=2), encoding='utf-8')
            print('round %d/%d %s: checksums %d/%d' % (round_number, options.rounds, name, len(CASES), len(CASES)), flush=True)

    names = [name for name, _, _, _ in programs]
    median = {case: {} for case in CASES}
    spread = {case: {} for case in CASES}
    for case in CASES:
        for name in names:
            values = [run['rows'][case]['microseconds'] / 1000.0 for run in data['runs']
                      if not run['warmup'] and run['language'] == name]
            median[case][name] = statistics.median(values)
            spread[case][name] = [min(values), max(values)]
    data['median_ms'] = median
    data['min_max_ms'] = spread
    data['checksums'] = expected
    data['completed_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / 'measurements.json').write_text(json.dumps(data, indent=2), encoding='utf-8')

    first = names[0]
    references = [name for name in names[1:]]
    totals = {name: sum(median[case][name] for case in CASES) for name in names}

    def cell(name, value):
        return '%.1f' % value if name == 'Cpp' else '%d' % round(value)

    lines = ['| Case | ' + ' | '.join('%s (ms)' % name for name in names) + ' | '
             + ' | '.join('%s vs %s' % (name, first) for name in references) + ' | Checksum |',
             '| --- | ' + ' | '.join('---:' for _ in names) + ' | ' + ' | '.join('---:' for _ in references) + ' | ---: |']
    for case in CASES:
        lines.append('| `%s` | ' % case + ' | '.join(cell(name, median[case][name]) for name in names) + ' | '
                     + ' | '.join('%.2fx' % (median[case][first] / median[case][name]) for name in references)
                     + ' | %d |' % expected[case])
    lines.append('| **total** | ' + ' | '.join(cell(name, totals[name]) for name in names) + ' | '
                 + ' | '.join('%.2fx' % (totals[first] / totals[name]) for name in references) + ' | |')
    lines += ['', '| Case | ' + ' | '.join('%s min-max' % name for name in names) + ' |',
              '| --- | ' + ' | '.join('---:' for _ in names) + ' |']
    for case in CASES:
        lines.append('| `%s` | ' % case + ' | '.join('%s-%s' % (cell(name, spread[case][name][0]), cell(name, spread[case][name][1]))
                                                     for name in names) + ' |')
    lines += ['', 'Executables:']
    for name in names:
        entry = data['executables'][name]
        offset = entry.get('dispatch_offset')
        lines.append('- %s: `%s` sha256 `%s`%s' % (name, entry['path'], entry['sha256'][:16],
                                                   '' if offset is None else ', dispatch offset %d' % offset))
    for name in references:
        lines.append('- Sum of case medians: %s is %.1f%% %s %s.' % (
            first, abs(1 - totals[first] / totals[name]) * 100, 'below' if totals[first] < totals[name] else 'above', name))
    lines.append('- Session %s to %s UTC, %d rounds after one warmup; record `%s`.' % (
        data['started_utc'][:19], data['completed_utc'][:19], options.rounds, out / 'measurements.json'))
    (out / 'summary.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines), flush=True)


if __name__ == '__main__':
    main()
