# Brushless ESC PWM Notes

## PWM Duty Table

The table below converts common ESC pulse widths to duty cycle at different PWM frequencies.

| High pulse width | 50 Hz | 100 Hz | 200 Hz | 250 Hz | 500 Hz | Meaning |
|---:|---:|---:|---:|---:|---:|---|
| 1000 us | 5.00% | 10.00% | 20.00% | 25.00% | 50.00% | Arming / minimum throttle |
| 1050 us | 5.25% | 10.50% | 21.00% | 26.25% | 52.50% | Near startup |
| 1079 us | 5.40% | 10.79% | 21.58% | 26.98% | 53.95% | Measured startup point |
| 1100 us | 5.50% | 11.00% | 22.00% | 27.50% | 55.00% | Near startup |
| 1500 us | 7.50% | 15.00% | 30.00% | 37.50% | 75.00% | Mid throttle / neutral for bidirectional ESC |
| 2000 us | 10.00% | 20.00% | 40.00% | 50.00% | 100.00% | Full throttle |

PWM periods:

| Frequency | Period |
|---:|---:|
| 50 Hz | 20000 us |
| 100 Hz | 10000 us |
| 200 Hz | 5000 us |
| 250 Hz | 4000 us |
| 500 Hz | 2000 us |

Formula:

```text
duty cycle = high pulse width / PWM period
PWM period = 1 / frequency
```

## Pulse Width, Not Just Duty Cycle

For a common brushless ESC using servo-style PWM input, throttle is mainly determined by the high pulse width, not by duty cycle alone.

For example, these commands are intended to represent roughly the same throttle because they all use about `1079 us` high pulse width:

```text
50 Hz  / 5.40%
100 Hz / 10.79%
200 Hz / 21.58%
250 Hz / 26.98%
500 Hz / 53.95%
```

By contrast, keeping the same duty cycle while changing frequency changes the actual high pulse width, so the ESC may see a completely different throttle command.

Example with `25%` duty cycle:

```text
50 Hz:  20000 us period * 25% = 5000 us
100 Hz: 10000 us period * 25% = 2500 us
200 Hz: 5000 us period * 25% = 1250 us
250 Hz: 4000 us period * 25% = 1000 us
500 Hz: 2000 us period * 25% = 500 us
```

So when debugging ESC throttle, set the target by pulse width first:

```text
Arming / minimum throttle: 1000 us
Startup test:              1050-1100 us
Medium throttle:           1300-1500 us
Higher throttle:           1600-1800 us
Full throttle:             2000 us
```

Then convert that pulse width to the duty cycle required by the selected PWM frequency.

## Frequency And Torque

If the ESC supports the selected input frequency and the high pulse width is the same, the commanded throttle is usually the same. Raising the input frequency from `50 Hz` to `200 Hz` or `250 Hz` may improve throttle update response, but it usually does not directly increase steady-state torque.

Motor torque is affected more directly by the ESC's internal control algorithm, battery voltage, current limit, motor KV, motor winding, propeller/load, and whether the ESC has armed correctly.

If one frequency feels stronger than another, common causes are:

- The ESC recognizes one input frequency more reliably.
- The actual measured high pulse width is different.
- The ESC throttle range or arming sequence changed.
- The signal level, grounding, or wiring is marginal.

