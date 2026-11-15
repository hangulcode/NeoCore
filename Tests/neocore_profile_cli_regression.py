import pathlib
import subprocess
import sys
import tempfile


def main():
    runner = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="neocore-profile-") as directory:
        source = pathlib.Path(directory) / "profile.nc"
        source.write_text(
            "int Leaf(){return 3;}\n"
            "void Root(){print(Leaf()+Leaf());}\nRoot();\n", encoding="utf-8")
        result = subprocess.run([str(runner), "--debug", "--profile", "--file", str(source)],
                                capture_output=True, text=True, encoding="utf-8", timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        rows = [line.split("\t") for line in result.stdout.splitlines() if line.startswith("[PROFILE] script\t")]
        counts = {row[5]: int(row[1]) for row in rows}
        assert counts["Leaf"] == 2 and counts["Root"] == 1, rows
        times = [float(row[2]) for row in rows]
        assert times == sorted(times, reverse=True), rows
        assert abs(sum(float(row[4]) for row in rows) - 100) < 0.1, rows
        assert all(float(row[3]) >= float(row[2]) for row in rows), rows
        source.write_text("int Broken(){int z=0;return 1/z;}\nprint(Broken());", encoding="utf-8")
        result = subprocess.run([str(runner), "--debug", "--profile", "--file", str(source)],
                                capture_output=True, text=True, encoding="utf-8", timeout=20)
        assert result.returncode == 1, result.stdout + result.stderr
        assert "Call stack (innermost first)" in result.stderr and "Broken at" in result.stderr, result.stderr
        assert "[PROFILE] script" in result.stdout and "\tBroken\t" in result.stdout, result.stdout
        invalid = subprocess.run([str(runner), "--profile", "--check", str(source)],
                                 capture_output=True, timeout=20)
        assert invalid.returncode == 2, invalid.stderr
    print("profile CLI regression: passed")


if __name__ == "__main__":
    main()
