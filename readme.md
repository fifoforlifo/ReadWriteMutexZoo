# ReadWriteMutexZoo

This is a small project that allowed me to play with various read-write mutex
designs.  There is a simple testbench that can measure stats such as
reads/sec, writes/sec, and some fairness ratios.

*None of the code here is intended for direct use in production.*
*It is here for you to adapt to your own needs and uses.*

Additionally, the implementations here are for Windows, which has its own
peculiar performance behavior for its built-in synchronization primitives.
Therefore, when porting to other platforms, different primitives in the
same exact algorithms may result in dramatically different output.

## Summary of Results

OS primitives tend to provide lock frequency of 1MHz - 50MHz and
performance tends to degrade as the number of contending threads increases.

The asymmetric mutexes in this library can provide reader-lock frequency
in the 70MHz - 1GHz range, generally at the expense of writer frequency.
In all cases there is a guarantee of no-starvation, but depending on
the OS scheduler, some mutexes are markedly less fair than others.

There is also an observed trade-off between total throughput (combined
reader and writer locks per second), and fairness.

## Specific Designs and their Uses

TODO ... 
The benchmark charts in "Benchmarks.ods" speak volumes.

## Reference Material

I came up with most of the algorithms in this library by myself;
notable exceptions are the following:
- QtReadWriteMutex : based on http://doc.trolltech.com/qq/qq11-mutex.html
- FairReadWriteMutex : based on http://vorlon.case.edu/~jrh23/338/HW3.pdf

However, I wanted to go back and see if such algorithms exist already.
In general, the fast algorithms in this library are related to the
"Dekker Lock", which turns out to be one of the first mutex algorithms!
    http://en.wikipedia.org/wiki/Dekker%27s_algorithm

Another keyword is "biased lock".  Google found this good article:
    http://blogs.oracle.com/dave/entry/biased_locking_in_hotspot

