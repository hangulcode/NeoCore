import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--console", required=True)
    options = parser.parse_args()
    console = Path(options.console).resolve()
    checks = 0
    with tempfile.TemporaryDirectory(prefix="neocore-module-") as temporary:
        directory = Path(temporary) / "unicode-\uac80\uc99d"
        directory.mkdir()
        (directory / "nested").mkdir()

        def write(name, source):
            path = directory / name
            path.write_text(source, encoding="utf-8-sig")
            return path

        def run(path):
            return subprocess.run([str(console), "--debug", "--file", str(path)],
                                  capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15)

        write("state.nc", "public int value=1;value+=1;")
        second_name = "STATE" if os.name == "nt" else "state"
        write("root.nc", "import state as first;first.value=7;import " + second_name + " as second;"
              "if(second.value!=7){int zero=0;print(1/zero);}print(\"module identity ok\");")
        result = run(directory / "nested" / ".." / "root.nc")
        assert result.returncode == 0 and "module identity ok" in result.stdout, result.stdout + result.stderr
        checks += 1

        self_import = write("self.nc", "import self;public int Read(){return 1;}")
        result = run(directory / "nested" / ".." / self_import.name)
        assert result.returncode != 0 and "cyclic module import" in result.stderr, result.stdout + result.stderr
        assert "self.nc:1:" in result.stderr, result.stderr
        checks += 1

        write("wrong.nc", "// module diagnostic\npublic int Read(){return \"wrong\";}")
        diagnostic_root = write("diagnostic.nc", "import wrong;print(wrong.Read());")
        result = run(diagnostic_root)
        assert result.returncode != 0 and "wrong.nc:2:" in result.stderr, result.stdout + result.stderr
        checks += 1

    print("NeoCore module CLI regression: " + str(checks) + " checks passed")


if __name__ == "__main__":
    main()
