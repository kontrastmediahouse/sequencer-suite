import subprocess, random, sys
import os
JS = os.environ.get('TGB_JS', os.path.join(os.path.dirname(__file__), 'tgb420.js'))
def run(cfg):
    txt = '\n'.join(cfg) + '\n'
    a = subprocess.run(['node', 'diff_js.js', JS], input=txt, capture_output=True, text=True).stdout.split('\n')
    b = subprocess.run(['./diff_bee'], input=txt, capture_output=True, text=True).stdout.split('\n')
    a = [l.split() for l in a if l.strip()]; b = [l.split() for l in b if l.strip()]
    return a, b
def compare(name, cfg):
    a, b = run(cfg)
    if len(a) != len(b):
        return f'{name}: count js={len(a)} cpp={len(b)}  first js={a[:3]} cpp={b[:3]}'
    for x, y in zip(a, b):
        if abs(float(x[0]) - float(y[0])) > 1.0 or x[1] != y[1] or x[2] != y[2]:
            return f'{name}: mismatch js={x} cpp={y}'
        dj, dc = float(x[3]), float(y[3])
        if abs(dj - dc) > 1.0 and not (dc < dj):     # C++ may cut a tie short by a retrigger
            return f'{name}: dur js={x} cpp={y}'
    return None
R = random.Random(7)
def arr(f): return ' '.join(str(f(i)) for i in range(16))
cases = []
cases.append(('defaults', ['quarters 8']))
for c in range(60):
    cfg = [
        'pitch ' + arr(lambda i: R.randint(-36, 36)),
        'oct ' + arr(lambda i: R.randint(-2, 2)),
        'vel ' + arr(lambda i: R.randint(1, 127)),
        'gate ' + arr(lambda i: R.choice([5, 25, 50, 75, 99, 100])),
        'mode ' + arr(lambda i: R.randint(0, 3)),
        'div ' + arr(lambda i: R.randint(0, 3)),
        'len ' + arr(lambda i: R.choice([0, 1, 1, 2, 3, 4, 8])),
        f'length {R.randint(1, 16)}',
        f'direction {R.choice([0, 1, 2])}',
        f'transpose {R.randint(-24, 24)}',
        'llen ' + ' '.join(str(R.choice([0, 0, 0, R.randint(1, 16)])) for _ in range(5)),
        f'clock {R.randint(0, 3)}',
        f'swing {R.choice([0, 0, 30, 100])}',
        f'countby {R.choice([0, 0, 1])}', f'pulses {R.randint(1, 64)}',
        f'scalemode {R.choice([0, 1])}', f'root {R.randint(0, 11)}', f'scale {R.randint(0, 15)}',
        'quarters 32',
    ]
    pm = R.choice([0, 0, 1, 2])
    cfg.append(f'playmode {pm}')
    if pm == 1: cfg.append(f'bassnote {R.randint(24, 60)}')
    if pm == 2:
        cfg.append('held ' + ' '.join(str(R.randint(48, 72)) for _ in range(R.randint(1, 5))))
        cfg.append(f'arporder {R.choice([o for o in range(17) if o != 14])}')
        cfg.append(f'arpoct {R.randint(1, 4)}')
        cfg.append(f'arpseed {R.randint(1, 100000)}')
    cases.append((f'random{c} pm{pm}', cfg))
fails = [r for r in (compare(n, c) for n, c in cases) if r]
print(f'{len(cases) - len(fails)}/{len(cases)} configurations identical')
for f in fails[:10]: print(f)
