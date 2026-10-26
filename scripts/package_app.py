"""Smoke-test an installed standalone package without development Qt paths."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

import cv2
import numpy as np


def package_environment() -> dict[str, str]:
	# Remove Qt paths injected by install-qt-action so deployment is actually tested.
	environment = os.environ.copy()
	for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH",
		"QML_IMPORT_PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
		environment.pop(key, None)
	qt_root = environment.get("QT_ROOT_DIR", "")
	if qt_root:
		environment["PATH"] = os.pathsep.join(entry for entry in environment["PATH"].split(os.pathsep)
			if not str(Path(entry)).lower().startswith(str(Path(qt_root)).lower()))
	environment["QT_QPA_PLATFORM"] = "windows" if sys.platform == "win32" else "cocoa" if sys.platform == "darwin" else "xcb"
	environment["QT_QUICK_BACKEND"] = "software"

	return environment


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--package-dir", required=True, type=Path)
	args = parser.parse_args()
	package = args.package_dir.resolve()
	windows = sys.platform == "win32"
	macos = sys.platform == "darwin"
	if macos:
		bundle = package / "nasti-pcb-designer.app/Contents"
		app = bundle / "MacOS/nasti-pcb-designer"
		exporter_directory = bundle / "Resources/exporter"
	else:
		app = package / "bin" / ("nasti-pcb-designer.exe" if windows else "nasti-pcb-designer")
		exporter_directory = package / "exporter"
	exporter = exporter_directory / ("pcb-exporter.exe" if windows else "pcb-exporter")
	for executable in (app, exporter):
		if not executable.is_file():
			raise FileNotFoundError(executable)

	environment = package_environment()

	with tempfile.TemporaryDirectory(prefix="nasti-package-") as directory:
		working = Path(directory)
		image = working / "input.png"
		cv2.imwrite(str(image), np.array([[0, 255], [255, 0]], dtype=np.uint8))
		manifest = working / "manifest.json"
		manifest.write_text(json.dumps({
			"canvas": {"width": 2, "height": 2, "widthMm": 10, "heightMm": 10},
			"layers": [{"source": str(image), "type": "enig", "side": "front", "x": 0, "y": 0}],
		}), encoding="utf-8")
		output = working / "output.epro2"
		subprocess.run([str(exporter), "--manifest", str(manifest), "-o", str(output)],
			cwd=working, env=environment, check=True, timeout=120)
		with zipfile.ZipFile(output) as archive:
			if not archive.namelist() or archive.testzip() is not None:
				raise RuntimeError("Bundled exporter produced an invalid PCB archive")
		screenshot = working / "smoke.png"
		subprocess.run([str(app), "--smoke-screenshot", str(screenshot)],
			cwd=working, env=environment, check=True, timeout=60)
		if not screenshot.is_file() or screenshot.stat().st_size == 0:
			raise RuntimeError("Packaged Qt app did not render its QML scene")

	print(f"Package verified: {package}")


if __name__ == "__main__":
	main()
