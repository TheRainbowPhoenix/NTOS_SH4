#!/usr/bin/env python3
"""bisect_engine.py — run the engine test one op at a time to find the hang."""
import subprocess
import sys

TEST = '/tmp/engine_test.c'
IMARK = 'int main(void)\n{\n'

OPS = [
    'show("12+3", dec_add(dec_i(12), dec_i(3)));',
    'show("12*3", dec_mul(dec_i(12), dec_i(3)));',
    'show("12/3", dec_div(dec_i(12), dec_i(3)));',
    'show("1/3", dec_div(dec_i(1), dec_i(3)));',
    'show("sqrt(2)", dec_sqrt(dec_i(2)));',
    'show("sqrt(9)", dec_sqrt(dec_i(9)));',
    'show("sin(pi/6)", dec_sin(dec_div(dec_pi(), dec_i(6))));',
    'show("cos(pi/6)", dec_cos(dec_div(dec_pi(), dec_i(6))));',
    'show("tan(pi/4)", dec_tan(dec_div(dec_pi(), dec_i(4))));',
    'show("sin(30)", dec_sin(dec_mul(dec_i(30), dec_div(dec_pi(), dec_i(180)))));',
    'show("asin(0.5)", dec_asin(dec_div(dec_i(1), dec_i(2))));',
    'show("atan(1)", dec_atan(dec_i(1)));',
    'show("ln(e)", dec_ln(dec_e()));',
    'show("log10(100)", dec_log10(dec_i(100)));',
    'show("exp(1)", dec_exp(dec_i(1)));',
    'show("2^10", dec_pow(dec_i(2), dec_i(10)));',
    'show("sinh(1)", dec_sinh(dec_i(1)));',
    'show("tanh(1)", dec_tanh(dec_i(1)));',
    'show("10!", dec_fact(dec_i(10)));',
    'show("sin(2pi+pi/6)", dec_sin(dec_add(dec_mul(dec_pi(), dec_i(2)), dec_div(dec_pi(), dec_i(6)))));',
    'show("999999^2", dec_mul(dec_i(999999), dec_i(999999)));',
    'show("1e10+2", dec_add((Dec){1,10}, dec_i(2)));',
    'show("1e-10+1", dec_add((Dec){1,-10}, dec_i(1)));',
]

src = open(TEST).read()
base = src[:src.find(IMARK) + len(IMARK)]
tail = '    return 0;\n}\n'

for op in OPS:
    one = base + '    printf("%s...\\n");\n    ' + op + '\n' + tail
    one = one.replace('%s...', op.split('"')[1])
    open('/tmp/engine_one.c', 'w').write(one)
    r = subprocess.run(
        ['gcc', '-O1', '-I', 'edge/app', '-I', 'files/app', '-I', 'calc/app',
         '-I', 'payload/phone', '-o', '/tmp/engine_one', '/tmp/engine_one.c', '-lm'],
        capture_output=True, text=True)
    if r.returncode:
        print('BUILD FAIL', op, r.stderr[:200])
        continue
    try:
        r = subprocess.run(['/tmp/engine_one'], capture_output=True,
                           text=True, timeout=3)
        print(f"OK   {r.stdout.strip().replace(chr(10), ' | ')}")
    except subprocess.TimeoutExpired:
        print(f"HANG {op}")
        sys.exit(1)
print('all done')
