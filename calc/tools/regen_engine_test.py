#!/usr/bin/env python3
"""regen_engine_test.py — slice the engine part of calc_app.c (everything
before the 'engine state' marker) and append the host test harness."""
import sys

SRC = '/home/z/my-project/NTOS_SH4/calc/app/calc_app.c'
TEST = '/tmp/engine_test.c'
MARKER = ' * engine state (standard + scientific)'

HARNESS = r'''
/* ---- host test harness ---- */
#include <stdio.h>
static void show(const char *what, Dec v)
{
    char b[32];
    dec_to_string_buf(v, b, 32);
    printf("%-14s = %s\n", what, b);
}
int main(void)
{
    show("12+3", dec_add(dec_i(12), dec_i(3)));
    show("12*3", dec_mul(dec_i(12), dec_i(3)));
    show("12/3", dec_div(dec_i(12), dec_i(3)));
    show("1/3", dec_div(dec_i(1), dec_i(3)));
    show("2/3", dec_div(dec_i(2), dec_i(3)));
    show("sqrt(2)", dec_sqrt(dec_i(2)));
    show("sqrt(9)", dec_sqrt(dec_i(9)));
    show("sin(pi/6)", dec_sin(dec_div(dec_pi(), dec_i(6))));
    show("cos(pi/6)", dec_cos(dec_div(dec_pi(), dec_i(6))));
    show("tan(pi/4)", dec_tan(dec_div(dec_pi(), dec_i(4))));
    show("sin(30)", dec_sin(dec_mul(dec_i(30), dec_div(dec_pi(), dec_i(180)))));
    show("asin(0.5)", dec_asin(dec_div(dec_i(1), dec_i(2))));
    show("atan(1)", dec_atan(dec_i(1)));
    show("ln(e)", dec_ln(dec_e()));
    show("log10(100)", dec_log10(dec_i(100)));
    show("exp(1)", dec_exp(dec_i(1)));
    show("2^10", dec_pow(dec_i(2), dec_i(10)));
    show("sinh(1)", dec_sinh(dec_i(1)));
    show("tanh(1)", dec_tanh(dec_i(1)));
    show("10!", dec_fact(dec_i(10)));
    show("sin(2pi+pi/6)", dec_sin(dec_add(dec_mul(dec_pi(), dec_i(2)), dec_div(dec_pi(), dec_i(6)))));
    char b1[32];
    dec_to_string_buf((Dec){123456, -3}, b1, 32);
    printf("fmt 123.456     = %s\n", b1);
    dec_to_string_buf((Dec){123456789, -5}, b1, 32);
    printf("fmt 1234.56789  = %s\n", b1);
    dec_to_string_buf((Dec){-42, 0}, b1, 32);
    printf("fmt -42         = %s\n", b1);
    dec_to_string_buf((Dec){1, 21}, b1, 32);
    printf("fmt 1e21        = %s\n", b1);
    dec_to_string_buf((Dec){0, 0}, b1, 32);
    printf("fmt 0           = %s\n", b1);
    dec_to_string_buf((Dec){5, -3}, b1, 32);
    printf("fmt 0.005       = %s\n", b1);
    dec_to_string_buf((Dec){1000000, 0}, b1, 32);
    printf("fmt 1000000     = %s\n", b1);
    dec_to_string_buf((Dec){9999999999999999ll, -15}, b1, 32);
    printf("fmt ~1          = %s\n", b1);
    show("999999^2", dec_mul(dec_i(999999), dec_i(999999)));
    show("1e10+2", dec_add((Dec){1,10}, dec_i(2)));
    show("1e-10+1", dec_add((Dec){1,-10}, dec_i(1)));
    return 0;
}
'''


def main():
    d = open(SRC).read()
    i = d.find(MARKER)
    assert i > 0, 'marker not found'
    # back up to the start of the banner comment
    i = d.rfind('/*', 0, i)
    engine = d[:i]
    open(TEST, 'w').write(engine + HARNESS)
    print('engine slice:', len(engine), 'bytes ->', TEST)


if __name__ == '__main__':
    main()
