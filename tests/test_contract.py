from pathlib import Path
import gzip
import hashlib
import json
import re
import unittest
from html.parser import HTMLParser

ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / 'firmware/nodemcu_spruzzini'
MANIFEST = json.loads(ROOT.joinpath('docs/CORE-PROVENANCE.json').read_text())
OLD = ROOT.joinpath('controller_irrigazione_anonimo.ino').read_text(encoding='utf-8-sig')
NEW = SKETCH.joinpath('nodemcu_spruzzini.ino').read_text(encoding='utf-8-sig')

def function(text, name):
    start = re.search(r'^(?:void|bool|String|long int)\s+' + name + r'\([^\n]*\)\s*\{', text, re.M)
    if not start: raise AssertionError('Missing function: ' + name)
    tail = text[start.end():]
    level = 1
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|[{}]', tail):
        if token[0] == '{': level += 1
        elif token[0] == '}': level -= 1
        if level == 0: return text[start.start():start.end() + token.end()]
    raise AssertionError('Unclosed function: ' + name)

def normalized(text):
    # Public configuration moves the old default notification recipient into the allowlist.
    text = re.sub(r'bot.sendMessage\("[^"\n]+",', 'bot.sendMessage(chatConsentite[0],', text)
    return re.sub(r'\s+', ' ', text).strip()

class Contract(unittest.TestCase):
    def test_sanitized_historical_reference(self):
        data = ROOT.joinpath('controller_irrigazione_anonimo.ino').read_bytes()
        digest = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        self.assertEqual(digest, 'c69829b499bacd9417362813ae968528c2bae468')

    def test_vendored_core_matches_snapshot(self):
        for name, digest in MANIFEST['files'].items():
            self.assertEqual(hashlib.sha256(SKETCH.joinpath(name).read_bytes()).hexdigest(), digest)

    def test_unchanged_irrigation_weather_button_and_clock_functions(self):
        for name in ['controllaPulsante', 'avviaSettoreSingolo', 'pioggiaPrevista', 'aggiornaUltimaPioggia', 'recuperaMeteoJson', 'orario', 'timestampPerCiclo', 'iniziaSequenzaIrrigazione', 'isChatAutorizzata']:
            with self.subTest(name=name): self.assertEqual(normalized(function(OLD, name)), normalized(function(NEW, name)))

    def test_entire_legacy_loop_unchanged_except_network_services(self):
        loop = function(NEW, 'loop').replace('  deviceCore.loop();\n  servizioRete();\n  if (!notificaAvvioInviata) return;\n', '')
        loop = loop.replace('(WiFi.status() == WL_CONNECTED ? bot.getUpdates(bot.last_message_received + 1) : 0)', 'bot.getUpdates(bot.last_message_received + 1)')
        self.assertEqual(normalized(function(OLD, 'loop')), normalized(loop))

    def test_pins_timing_and_legacy_struct_preserved(self):
        for pattern in [r'const int (?:numSettori|pinSettori\[numSettori\]|pinPulsante|pinLed) = [^;]+;', r'const (?:long|int) (?:gmtOffset_sec|daylightOffset_sec) = [^;]+;', r'unsigned long orePrecedentiPioggia = [^;]+;', r'struct Impostazioni \{[\s\S]*?\n\};']:
            self.assertEqual(re.findall(pattern, OLD), re.findall(pattern, NEW))

    def test_no_eeprom_application_writes_or_second_http_server(self):
        storage = SKETCH.joinpath('spruzzini_storage.h').read_text()
        interface = SKETCH.joinpath('spruzzini_interface.h').read_text()
        self.assertNotIn('EEPROM.put', NEW + storage + interface)
        self.assertNotIn('EEPROM.commit', NEW + storage + interface)
        self.assertNotIn('WiFiServer', NEW + interface)
        self.assertIn('LittleFSConfig(false)', storage)
        self.assertIn('!memoriaPronta && partizioneVuota()', storage)
        self.assertIn('!LittleFS.rename', storage)
        self.assertLess(NEW.index('digitalWrite(pinSettori[i], HIGH)'), NEW.index('caricaDati();'))
        self.assertLess(NEW.index('caricaDati();'), NEW.index('deviceCore.begin();'))

    def test_public_controls_keep_csrf_and_maintenance_rejected_before_upload(self):
        source = SKETCH.joinpath('spruzzini_interface.h').read_text()
        self.assertTrue(function(source, 'comandoSpruzzini').startswith('void comandoSpruzzini() {\n  if (!autorizzaScrittura()) return;'))
        self.assertIn('web.header("X-CSRF-Token") != tokenSpruzzini', source)
        self.assertNotIn("autenticato(", source)
        self.assertNotIn(".authenticate(", source)
        self.assertNotIn(".requestAuthentication(", source)
        self.assertIn('web.on("/api/spruzzini/status", HTTP_GET, inviaStatoSpruzzini)', source)
        core = SKETCH.joinpath("ESPDeviceCore.cpp").read_text()
        self.assertIn("server_.authenticate", core)
        self.assertIn("server_.requestAuthentication", core)
        self.assertIn('web.addHook', source)
        for path in ['/admin/api/update', '/admin/api/restart', '/admin/api/config']:
            self.assertIn('url == "' + path + '"', source)
        self.assertIn('return DeviceWebServer::CLIENT_MUST_STOP', source)

    def test_embedded_assets_identical_to_sources(self):
        header = SKETCH.joinpath('spruzzini_web.h').read_text()
        for name, file in [('SPRUZZINI_INDEX', 'index.html'), ('SPRUZZINI_CSS', 'css/custom.css'), ('SPRUZZINI_JS', 'js/spruzzini.js')]:
            block = re.search(name + r'\[\] PROGMEM = \{([\s\S]*?)\};', header)[1]
            self.assertEqual(gzip.decompress(bytes(map(int, re.findall(r'\d+', block)))), ROOT.joinpath('web', file).read_bytes())

    def test_web_ids_exist_and_unique(self):
        class Tags(HTMLParser):
            def __init__(self): super().__init__(); self.ids = []
            def handle_starttag(self, tag, attrs):
                for key, value in attrs:
                    if key == 'id': self.ids.append(value)
        parser = Tags(); parser.feed(ROOT.joinpath('web/index.html').read_text(encoding='utf-8'))
        self.assertEqual(len(parser.ids), len(set(parser.ids)))
        for id_ in re.findall(r"el\('([^']+)'\)", ROOT.joinpath('web/js/spruzzini.js').read_text()): self.assertIn(id_, parser.ids)

if __name__ == '__main__': unittest.main(verbosity=2)
