import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--console", required=True)
    options = parser.parse_args()
    console = Path(options.console).resolve()
    root = Path(__file__).resolve().parents[1]
    sample_names = {path.stem for path in (root / "Samples/neocore/scripts").glob("*.nc")}
    automatic_names = sample_names - {"performance", "https"}
    checks = 0

    def run(arguments=(), commands="", cwd=root, timeout=30, expected_success=True):
        result = subprocess.run([str(console)] + list(arguments), input=commands, cwd=cwd,
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=timeout)
        assert (result.returncode == 0) == expected_success, (
            "command: " + repr(arguments) + "\ninput: " + repr(commands) + "\n"
            + result.stdout + result.stderr)
        assert "FAIL|" not in result.stdout + result.stderr, result.stdout + result.stderr
        return result

    def passed(result):
        return re.findall(r"\bsample ([a-zA-Z0-9_]+) passed\b", result.stdout)

    def check_all(result):
        expected = automatic_names - {"divide_by_zero"}
        completed = passed(result)
        assert set(completed) == expected, result.stdout + result.stderr
        assert len(completed) == len(expected), result.stdout + result.stderr
        assert result.stdout.count("Expected division-by-zero error handled.") == 1, result.stdout
        assert "Functional samples: 26 passed, 0 failed." in result.stdout, result.stdout
        assert "Skipped: performance (benchmark), https (live network)." in result.stdout, result.stdout

    result = run(["--list"])
    entries = re.findall(r"^\s*(\d+)(?:[.)]|\s)\s*([a-zA-Z0-9_]+)\b", result.stdout, re.MULTILINE)
    numbers = {name: number for number, name in entries}
    assert set(numbers) == sample_names, result.stdout + result.stderr
    assert len(entries) == len(sample_names), result.stdout
    assert len({number for number, _ in entries}) == len(entries), result.stdout
    assert numbers["hello"] == "0" and numbers["performance"] == "1", result.stdout
    assert not passed(result), result.stdout
    checks += 1

    result = run(commands="r\n" + numbers["hello"] + "\nfibonacci\ndivide_by_zero\nhello\nr\nq\n")
    assert passed(result) == ["hello", "fibonacci", "hello", "hello"], result.stdout + result.stderr
    assert result.stdout.count("Hello NeoCore ~^^~") == 3, result.stdout
    assert re.search(r"(?m)\b55\s*$", result.stdout), result.stdout
    assert result.stdout.count("Expected division-by-zero error handled.") == 1, result.stdout
    checks += 1

    result = run(["--debug"], "not_a_sample\n999999\nlist\nfibonacci\nquit\n")
    assert passed(result) == ["fibonacci"], result.stdout + result.stderr
    assert re.search(r"(?m)\b55\s*$", result.stdout), result.stdout
    checks += 1

    for arguments in ((), ("--debug",)):
        result = run(arguments)
        assert "hello" in result.stdout and "fibonacci" in result.stdout, result.stdout
        assert not passed(result), result.stdout
        checks += 1

    result = run(["--run", "hello"], cwd=console.parent)
    assert passed(result) == ["hello"], result.stdout + result.stderr
    checks += 1

    for sample, bindings in (("hello", set()),
                             ("callback", {"b1", "g0"}),
                             ("map_callback", {"b1", "b2", "g0"})):
        result = run(["--debug", "--asm", "--run", sample], cwd=console.parent)
        assert passed(result) == [sample], result.stdout + result.stderr
        registered = re.findall(r"(?m)^Global E \[-?\d+\] (\S+)", result.stdout)
        assert set(registered) == {"$neocore_host_" + name for name in bindings}, result.stdout
        assert len(registered) == len(bindings), result.stdout
        checks += 1

    for flag in ("--run-all", "--smoke", "--debug-smoke"):
        result = run([flag], cwd=console.parent, timeout=90)
        check_all(result)
        if flag == "--debug-smoke":
            assert "Debug source-location check passed." in result.stdout, result.stdout
        checks += 1
    for selection in ("all", "smoke", "debug-smoke"):
        result = run(commands=selection + "\nquit\n", timeout=90)
        check_all(result)
        if selection == "debug-smoke":
            assert "Debug source-location check passed." in result.stdout, result.stdout
        checks += 1

    for arguments in (("--compiler-error-regression",), ("--debug", "--compiler-error-regression")):
        result = run(arguments, cwd=console.parent)
        assert "NeoCore compiler-error regression: 24/24 passed" in result.stdout, result.stdout
        checks += 1

    source = str(root / "Samples/neocore/scripts/fibonacci.nc")
    for arguments in (("--debug", "--asm", "--stats", "--file", source),
                      ("--file", source, "--debug", "--asm", "--stats")):
        result = run(arguments, cwd=console.parent)
        assert re.search(r"(?m)\b55\s*$", result.stdout), result.stdout
        assert "Compile Success. Code :" in result.stdout, result.stdout
        assert "[ALLOC] process-global" in result.stdout, result.stdout
        assert result.stdout.rfind("[ALLOC]") > result.stdout.find("55\n"), result.stdout
        checks += 1

    invalid_source = str(root / "Samples/neocore/scripts/divide_by_zero.nc")
    result = run(("--file", invalid_source, "--debug"), expected_success=False)
    assert re.search(r"divide_by_zero\.nc:[1-9][0-9]*:[0-9]+:.*division by zero", result.stderr), result.stderr
    checks += 1

    for arguments in (("--bench-check",), ("--debug", "--bench-check")):
        result = run(arguments, cwd=console.parent)
        assert "Benchmark checks passed: 9 cases, 54 calls; no timing." in result.stdout, result.stdout
        assert "compile=" not in result.stdout and "run=" not in result.stdout, result.stdout
        checks += 1

    for arguments in (("--pooltrim-check",), ("--debug", "--pooltrim-check")):
        result = run(arguments, cwd=console.parent)
        assert "[pooltrim-check] passed" in result.stdout, result.stdout
        assert "budget=OK" in result.stdout, result.stdout
        assert "incremental max=" not in result.stdout and "idle average=" not in result.stdout, result.stdout
        checks += 1

    result = run(commands="compiler-errors\nbench-check\npooltrim-check\nq\n")
    assert "NeoCore compiler-error regression: 24/24 passed" in result.stdout, result.stdout
    assert "Benchmark checks passed: 9 cases, 54 calls; no timing." in result.stdout, result.stdout
    assert "[pooltrim-check] passed" in result.stdout, result.stdout
    checks += 1

    for arguments in (("--v2smoke",), ("--globals", "GameObject")):
        result = run(arguments, expected_success=False)
        assert "unsupported" in result.stderr or "not yet ported" in result.stderr, result.stderr
        assert not passed(result), result.stdout
        checks += 1

    with tempfile.TemporaryDirectory(prefix="neocore-menu-") as temporary:
        temporary_root = Path(temporary).resolve()
        assert temporary_root.parent == Path(tempfile.gettempdir()).resolve()
        scripts = temporary_root / "Samples/neocore/scripts"
        scripts.mkdir(parents=True)
        (scripts / "hello.nc").write_text('print("test sample discovery");', encoding="utf-8-sig")
        for sample in ("module", "particles"):
            (scripts / (sample + ".nc")).write_text(
                "export bool CheckPassed(){return false;}", encoding="utf-8-sig")
            result = run(("--run", sample), cwd=temporary_root, expected_success=False)
            assert "sample host result mismatch: " + sample in result.stderr, result.stderr
            assert not passed(result), result.stdout
            checks += 1

    print("NeoCore menu CLI regression: " + str(checks) + " checks passed")


if __name__ == "__main__":
    main()
