import argparse
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--console", required=True)
    console = pathlib.Path(parser.parse_args().console).resolve()
    checks = 0
    with tempfile.TemporaryDirectory(prefix="neocore-unused-") as temporary:
        root = pathlib.Path(temporary)

        def write(name, source):
            path = root / name
            path.write_text(source, encoding="utf-8-sig")
            return path

        def run(*arguments):
            return subprocess.run([str(console), *map(str, arguments)], capture_output=True,
                                  text=True, encoding="utf-8", errors="replace", timeout=20)

        entry = write("entry.nc", "const int DEAD=1;\nint unused;\nint written;written=2;\nvoid Never(){}\n")
        result = run("--check", entry)
        assert result.returncode == 0 and result.stderr == "", result.stdout + result.stderr
        checks += 1
        for debug in ([], ["--debug"]):
            result = run("--warn-unused", *debug, "--check", entry)
            assert result.returncode == 0 and "compilation passed" in result.stdout, result.stdout + result.stderr
            assert result.stderr.count("warning:") == 4, result.stderr
            for line, column, text in ((1, 11, "unused constant 'DEAD'"), (2, 5, "unused global variable 'unused'"),
                                       (3, 5, "global variable 'written' is written but never read"), (4, 6, "unused function 'Never'")):
                assert f"entry.nc:{line}:{column}: warning: {text}" in result.stderr, result.stderr
            checks += 1
        write("state.nc", "public const int LIVE=4;\npublic const int DEAD=5;\npublic int value=7;\npublic int Used(){return value;}\npublic void Never(){}\n")
        entry = write("entry.nc", "import state as first;import state as second;print(first.LIVE+second.Used());")
        result = run("--check", entry, "--warn-unused")
        assert result.returncode == 0 and result.stderr.count("warning:") == 2, result.stdout + result.stderr
        assert "state.nc:2:18: warning: unused constant 'DEAD'" in result.stderr, result.stderr
        assert "state.nc:5:13: warning: unused function 'Never'" in result.stderr, result.stderr
        assert "__ncmodule_" not in result.stderr and "$neocore_" not in result.stderr, result.stderr
        checks += 1
        write("exported.nc", "export int ModuleEntry(){return 1;}")
        entry = write("entry.nc", "import exported;export int RealEntry(){return 2;}export int External;")
        result = run("--warn-unused", "--check", entry)
        assert result.returncode == 0 and result.stderr.count("warning:") == 1 and "unused function 'ModuleEntry'" in result.stderr, result.stdout + result.stderr
        checks += 1
        write("private.nc", "const int LIMIT=3;int state;int Helper(){return LIMIT+state;}void Unused(){}")
        entry = write("entry.nc", "import private as p;print(p.Helper());")
        result = run("--warn-unused", "--check", entry)
        assert result.returncode == 1 and "not public" in result.stderr and "warning:" not in result.stderr, result.stderr
        checks += 1
        result = run("--warn-unused", "--entry-private-access", "--check", entry)
        assert result.returncode == 0 and result.stderr.count("warning:") == 1 and "unused function 'Unused'" in result.stderr, result.stdout + result.stderr
        checks += 1
        entry = write("entry.nc", "const int DEAD=4;int broken=true;")
        result = run("--check", entry, "--warn-unused")
        assert result.returncode == 1 and "warning:" not in result.stderr and "cannot convert" in result.stderr, result.stdout + result.stderr
        checks += 1
        entry = write("entry.nc", "void Dead(){}print(\"MUST_NOT_RUN\");")
        result = run("--check", entry, "--warn-unused")
        assert result.returncode == 0 and "MUST_NOT_RUN" not in result.stdout and "unused function 'Dead'" in result.stderr, result.stdout + result.stderr
        checks += 1
        for arguments in ([], ["--file", entry], ["--run", "hello"], ["--emit-backend", entry], ["--smoke"], ["--dap"], ["--lsp"]):
            result = run("--warn-unused", *arguments)
            assert result.returncode == 2 and "--warn-unused requires --check" in result.stderr, result.stdout + result.stderr
            checks += 1
    print(f"NeoCore unused CLI regression: {checks} checks passed")


if __name__ == "__main__":
    main()
