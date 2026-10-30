"""Compile a privately configured ESP8266 image and identity-checked EDC package."""
from pathlib import Path
import argparse
import importlib.util
import json
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arduino-cli', default=shutil.which('arduino-cli') or 'arduino-cli')
args = parser.parse_args()
sketch = ROOT / 'firmware/nodemcu_spruzzini'
if not (sketch / 'config_privata.h').is_file():
    sys.exit('Copy config_privata.example.h to config_privata.h and configure it first.')
subprocess.run([sys.executable, str(ROOT / 'tools/build_web.py')], check=True)
subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', str(ROOT / 'tests'), '-v'], check=True)
subprocess.run(['node', '--test', str(ROOT / 'tests/ui.test.js')], check=True)
subprocess.run([args.arduino_cli, 'compile', '--fqbn', 'esp8266:esp8266:nodemcuv2',
                '--build-path', str(ROOT / 'build'), str(sketch)], check=True)
spec = importlib.util.spec_from_file_location('package_firmware', ROOT / 'tools/package_firmware.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)
image = (ROOT / 'build/nodemcu_spruzzini.ino.bin').read_bytes()
package = packager.package(image)
info = packager.inspect_package(package)
if (info['board'], info['application']) != ('esp8266', 'nodemcu-spruzzini'):
    raise ValueError('Unexpected firmware identity')
output = ROOT / 'output'
output.mkdir(exist_ok=True)
stem = 'nodemcu-spruzzini-' + info['version']
(output / (stem + '.bin')).write_bytes(image)
(output / (stem + '.edc')).write_bytes(package)
(ROOT / 'build-report.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
print('Private outputs created in output/: ' + stem + '.bin / .edc')
