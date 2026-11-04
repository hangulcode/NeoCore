import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--minigame", required=True)
    options = parser.parse_args()
    executable = Path(options.minigame).resolve()
    scripts = Path(__file__).resolve().parents[1] / "Samples/neocore/minigame/scripts"
    checks = 0

    with tempfile.TemporaryDirectory(prefix="neocore-minigame-") as temporary:
        directory = Path(temporary).resolve()
        chosen = directory / "chosen scripts \uac80\uc99d"
        chosen.mkdir()
        for name in ("mover.nc", "replay.nc"):
            shutil.copy2(scripts / name, chosen / name)

        def run(arguments, expected=None, binary=executable):
            nonlocal checks
            result = subprocess.run([str(binary)] + list(arguments), cwd=directory,
                                    capture_output=True, text=True, encoding="utf-8",
                                    errors="replace", timeout=15)
            if expected is None:
                assert result.returncode == 0, result.stdout + result.stderr
            else:
                assert result.returncode == 1 and expected in result.stderr, result.stdout + result.stderr
            checks += 1
            return result

        for mode, name, success in (([], "mover.nc", "NeoCore minigame: all checks passed"),
                                    (["--motion"], "replay.nc", "NeoCore motion: all checks passed")):
            help_result = run(mode + ["--help"])
            assert "--file" in help_result.stdout
            run(mode, "requires --file")
            for value in ([], [""], ["--verify"]):
                run(mode + ["--file"] + value, "--file needs a path")
            run(mode + ["--file", str(directory / "missing.nc")], "cannot read script:")
            result = run(mode + ["--verify", "--file", str(scripts / name)])
            assert success in result.stdout, result.stdout
            result = run(["--file", str((chosen / name).relative_to(directory))] + mode + ["--verify"])
            assert success in result.stdout, result.stdout
            invalid = directory / "invalid.nc"
            invalid.write_text("int broken = ;", encoding="utf-8-sig")
            result = run(mode + ["--file", str(invalid)], "NeoCore minigame failed:")
            assert "cannot read script:" not in result.stderr, result.stderr

        recording = chosen / "motion recording.js"
        result = run(["--script", str(chosen / "replay.nc"), "--verify", "--record", str(recording)])
        assert "NeoCore motion: all checks passed" in result.stdout, result.stdout
        text = recording.read_text(encoding="utf-8")
        prefix = "window.neoScene = "
        assert text.startswith(prefix) and text.endswith(";\n"), text
        data = json.loads(text[len(prefix):-2])
        assert data["fps"] == 60 and len(data["frames"]) == 121, data
        assert all(len(frame) == 2 and all(len(position) == 3 for position in frame)
                   for frame in data["frames"])
        run(["--record", str(recording)], "requires --file")
        run(["--motion", "--file", str(chosen / "replay.nc"), "--record"], "--record needs a path")

        relocated = directory / executable.name
        shutil.copy2(executable, relocated)
        for mode, name in (([], "mover.nc"), (["--motion"], "replay.nc")):
            run(mode, "requires --file", binary=relocated)
            result = run(mode + ["--verify", "--file", str(chosen / name)], binary=relocated)
            assert "all checks passed" in result.stdout, result.stdout

    print("NeoCore minigame CLI regression: " + str(checks) + " checks passed")


if __name__ == "__main__":
    main()
