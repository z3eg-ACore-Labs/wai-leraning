"""Opt-in PyTorch WAI action client for the local authenticated server SOAP endpoint.

Nothing is sent until the player grants an in-game .wai start lease. Movement,
target choice, autoattack and looting are explicit helpers, not learned behavior.
"""
import argparse
import base64
import json
import math
import os
import re
from pathlib import Path
import time
import urllib.parse
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape


class Soap:
    def __init__(self, url, user, password, command_prefix='wai '):
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme != 'http' or parsed.hostname not in {'localhost', '127.0.0.1', '::1'}:
            raise ValueError('The first WAI action client only supports loopback HTTP SOAP')
        if parsed.username or parsed.password or not user or not password:
            raise ValueError('Set WAI_SOAP_USER and WAI_SOAP_PASSWORD; never put credentials in the URL')
        self.url = url
        if command_prefix not in {'wai ', 'waiepisode ', 'wainav '}:
            raise ValueError('Unsupported WAI command namespace')
        self.command_prefix = command_prefix
        self.authorization = 'Basic ' + base64.b64encode(f'{user}:{password}'.encode()).decode()

    def command(self, text):
        if not text.startswith(self.command_prefix) or any(c in text for c in '\r\n;'):
            raise ValueError('Only one WAI command is allowed')
        envelope = ("<s:Envelope xmlns:s='http://schemas.xmlsoap.org/soap/envelope/' xmlns:a='urn:AC'>"
                    '<s:Body><a:executeCommand><command>' + escape(text) +
                    '</command></a:executeCommand></s:Body></s:Envelope>')
        request = urllib.request.Request(self.url, envelope.encode(), headers={
            'Authorization': self.authorization, 'Content-Type': 'text/xml; charset=utf-8'})
        # No global proxy or redirect handler: credentials stay at the loopback endpoint.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
        # Only observations are safe to repeat after an uncertain response.
        # Three two-second reads stay below the episode's 15-second heartbeat.
        attempts = 3 if re.fullmatch(r'waiepisode (?:party [1-4] )?observe', text) else 1
        for attempt in range(attempts):
            try:
                with opener.open(request, timeout=2) as response:
                    body = response.read(65537)
                break
            except (TimeoutError, urllib.error.URLError) as error:
                timed_out = isinstance(error, TimeoutError) or isinstance(error.reason, TimeoutError)
                if not timed_out or attempt + 1 == attempts:
                    raise
        if len(body) > 65536 or b'<!DOCTYPE' in body.upper() or b'<!ENTITY' in body.upper():
            raise ValueError('Invalid SOAP response')
        root = ET.fromstring(body)
        if any(n.tag.split('}')[-1] == 'Fault' for n in root.iter()):
            raise RuntimeError('Server rejected the WAI command; check permissions and module availability')
        reply = next((n for n in root.iter() if n.tag.split('}')[-1] == 'executeCommandResponse'), None)
        if reply is None:
            raise ValueError('Missing SOAP command receipt')
        return ''.join(reply.itertext()).strip()


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError('SOAP redirects are not permitted')


def load_policy(path):
    # PyTorch is the production inference backend; the JSON artifact is a
    # fixed-shape export of our existing NumPy baseline, never pickle input.
    import torch
    from torch import nn
    model = json.loads(Path(path).read_text(encoding='utf-8'))
    if model.get('architecture') != [16, 12, 8, 6] or model.get('spells') != [779, 5232, 5487, 6808, 8618, 16857]:
        raise ValueError('This executor requires the trained 16-12-8-6 druid checkpoint')
    policy = nn.Sequential(nn.Linear(16, 12), nn.ReLU(), nn.Linear(12, 8), nn.ReLU(), nn.Linear(8, 6))
    with torch.no_grad():
        for module, saved in zip((policy[0], policy[2], policy[4]), model['layers']):
            weights = torch.tensor(saved['weights'], dtype=torch.float32).T
            bias = torch.tensor(saved['bias'], dtype=torch.float32)
            if weights.shape != module.weight.shape or bias.shape != module.bias.shape:
                raise ValueError('Invalid model parameter shape')
            if not torch.isfinite(weights).all() or not torch.isfinite(bias).all():
                raise ValueError('Non-finite model parameters')
            module.weight.copy_(weights)
            module.bias.copy_(bias)
    policy.eval()
    torch.set_num_threads(1)

    def infer(values):
        if len(values) != 16 or any(not math.isfinite(float(x)) or not 0 <= x <= 1 for x in values):
            raise ValueError('Invalid live feature vector')
        with torch.inference_mode():
            return torch.softmax(policy(torch.tensor(values, dtype=torch.float32)), dim=-1).tolist()
    return infer, model['spells']


def choose_action(observation, probabilities, spells):
    if observation['busy']:
        return 'wait', 'native/busy'
    if observation['lootable']:
        return ('loot' if observation['distance'] <= 4 else 'approach'), 'native/loot'
    if observation['features'][7] and observation['distance'] > 4:
        return 'approach', 'native/corpse-approach'
    # Engage physically so a queued Maul has an autoattack to attach to.
    if observation['enemy'] and not observation['attacking'] and observation['features'][4]:
        return 'attack', 'native/target-and-autoattack'
    allowed = set(observation['allowed_spells'])
    choices = [(probabilities[i], spell) for i, spell in enumerate(spells) if spell in allowed]
    if choices:
        _, spell = max(choices)
        return f'cast {spell}', 'learned/spell-choice-with-server-mask'
    if observation['enemy'] and observation['distance'] > 3:
        return 'attack', 'native/approach'
    return 'wait', 'native/no-legal-spell'


def load_route(path):
    from train import load_records
    records, _ = load_records(path)
    states = [r['state'] for r in records if r.get('kind') == 'state']
    if not states or any(s['map'] != 43 for s in states):
        raise ValueError('Route replay currently requires one Wailing Caverns recording')
    # Retain meaningful changes, not every 200ms position sample.
    points = []
    for state in states:
        p = state['self']['position'][:3]
        if not points or math.dist(points[-1], p) >= 2:
            points.append(p)
    return points


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--url', default='http://127.0.0.1:7878/')
    parser.add_argument('--route', type=Path, help='Explicit recorded route replay; movement is not learned')
    parser.add_argument('--log', type=Path, default=Path('output/wai-learning-demo/live-decisions.jsonl'))
    args = parser.parse_args()
    infer, spells = load_policy(args.model)
    soap = Soap(args.url, os.environ.get('WAI_SOAP_USER'), os.environ.get('WAI_SOAP_PASSWORD'))
    route = load_route(args.route) if args.route else []
    route_index = 0
    connected = False
    generation = None
    args.log.parent.mkdir(parents=True, exist_ok=True)
    print('WAI ready. In game: .wai start. Stop with .wai stop, manual input, or Ctrl+C.')
    try:
        with args.log.open('a', encoding='utf-8', buffering=1) as log:
            while True:
                receipt = soap.command('wai observe')
                if receipt == 'WAI_INACTIVE':
                    if connected:
                        print('Control released; waiting for a new in-game .wai start.')
                    connected = False
                    time.sleep(.2)
                    continue
                if not receipt.startswith('{'):
                    raise RuntimeError('WAI server module unavailable or rejected observation')
                obs = json.loads(receipt)
                if obs.get('schema') != 1:
                    raise ValueError('Unsupported live protocol')
                connected = True
                if generation != obs['generation']:
                    generation = obs['generation']
                    route_index = 0
                    print('Action lease acquired; learned spell policy active.')
                probabilities = infer(obs['features'])
                action, source = choose_action(obs, probabilities, spells)
                if route and action == 'wait' and not obs['busy'] and not obs['combat'] and obs['route_allowed']:
                    while route_index < len(route) and math.dist(obs['position'], route[route_index]) < 1.5:
                        route_index += 1
                    if route_index < len(route):
                        point = route[route_index]
                        if math.dist(obs['position'], point) > 8:
                            raise RuntimeError('Outside recorded route; move to its start before granting control')
                        action = 'move ' + ' '.join(f'{x:.4f}' for x in point)
                        source = 'scripted/recorded-route-replay'
                    else:
                        soap.command('wai stop')
                        print('Recorded route finished; control released. No dungeon-completion claim.')
                        break
                result = soap.command(f"wai act {generation} {obs['seq']} {action}")
                log.write(json.dumps({'time': time.time(), 'sequence': obs['seq'], 'source': source,
                                      'action': action, 'probabilities': probabilities, 'result': result}) + '\n')
                if action != 'wait':
                    print(f'{source}: {action} -> {result}')
                time.sleep(.3)
    except KeyboardInterrupt:
        print('Stopping WAI.')
    finally:
        # Stopping this administrator-authorized client revokes the server lease.
        # If the server is unreachable, the three-second lease expires independently.
        try:
            soap.command('wai stop')
        except Exception:
            pass


if __name__ == '__main__':
    main()
