Ubuntu Unity System Panel Indicator for Instantaneous Power
===========================================================

This program displays current power usage of various things in the system power, in the same was as the multiload indicator. Currently very hacky and fragile.

The indicator uses the battery dissipation level for the overall power draw, and displayed in Watts on the indicator, a running average is displayed in brackets. Four graphs show the power usage (as given by RAPL) for other items in the system.

The rapl program needs to have access to the RAPL counters. This means either it has to be run as root (setuid), or the sysctl parameter, kernel.perf\_event\_paranoid needs to be set to 0:

    sysctl kernel.perf_event_paranoid=0

This can also be put in /etc/sysctl.conf

To run:

    $ make
    $ ./indicator-power

Licenses
========

- Ubuntu mono. http://font.ubuntu.com/licence/
- RAPL read code. http://web.eece.maine.edu/~vweaver/projects/rapl/
- Everything else: GPLv3.
