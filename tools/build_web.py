from pathlib import Path
import gzip

root = Path(__file__).resolve().parents[1]
assets = [('SPRUZZINI_INDEX', 'index.html'), ('SPRUZZINI_CSS', 'css/custom.css'), ('SPRUZZINI_JS', 'js/spruzzini.js')]
header = '#pragma once\n#include <Arduino.h>\n'
for name, path in assets:
    data = gzip.compress(root.joinpath('web', path).read_bytes(), mtime=0)
    header += 'const uint8_t ' + name + '[] PROGMEM = {\n'
    header += '\n'.join(','.join(str(v) for v in data[i:i+24]) + ',' for i in range(0, len(data), 24))
    header += '\n};\n'
root.joinpath('firmware/nodemcu_spruzzini/spruzzini_web.h').write_text(header, encoding='utf-8')
print('Embedded web assets generated.')
