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

## Motivation

The concise goals are:

* Characterize existing mutex types, and understand their frequency ranges.
* Design new mutex types that are much lower overhead for readers when no writers
  are contending for a lock.


There are types of applications that can really benefit from high reader throughput:

* VMs that employ stop-the-world Garbage Collectors.

  Here, you can treat all mutator threads as readers and the collector as
  a writer.  When a collection needs to occur, simply take a write lock
  and all the mutators are booted out.

  If the overhead of taking a read-lock is negligible, then certain VM
  operations become *much* simpler.  For example, ensuring liveness of
  object references when a mutator only holds the reference in a machine
  register is no longer a problem, because the mutator may 'atomically'
  copy the reference from source-memory to stack-memory using many
  non-atomic instructions, all behind a read-lock.

* API trace

  You may have a need to intercept all functions of an API, perhaps to
  trace all enter and exits, or to boot all user code out of the API.
  Using an ordinary OS lock makes the API way slower when running the
  normal application code.

* A file-system cache, where traversal is expected to be a more frequent
  operation than modification or deletion of nodes.


OS primitives tend to provide lock frequency of 1MHz - 50MHz and
performance tends to degrade as the number of contending threads increases.

When considering using a lock, think of the alternative.  Imagine how your same
algorithm or API would work if ported to a uniprocessor machine, and was built
entirely using coroutines.  There would be no locks, no atomics required for
maintaining refcounts, etc.  Odds are, many programs would actually run
**faster** in a uniprocessor environment, because the thread-safety overhead
is just so high.

A funny way to put it, is that your new CPU frequency is the lock frequency.
So your program is really running between 1MHz - 50MHz.  That is slower than
a single-core Pentium processor released over 15 years ago!


## Summary of Interesting Results

The [UltraSpinReadWriteMutex](019_urwmutex/ultraspin_rwmutex.h) is really simple,
and has the best reader performance.  Eight cores get > 1GHz lock frequency.

The [FastSlimReadWriteMutex](019_urwmutex/fastslim_rwmutex.h) is like a "frontend"
on the Win32 SlimReadWriteLock.  Under load it performs similarly, but when only
readers are present, it achieves over an order of magnitude better perf.

The [UltraSyncSingleReadWriteMutex](019_urwmutex/ultrasync_single_rwmutex.h)
allows you to designate a single "reader" thread to get insane lock throughput (over 700MHz);
all other "writer" threads get ordinary access, mostly limited by CriticalSection.
This mutex can easily be generalized to support N *a priori* designated reader threads
who all achieve similar performance.


### Brief Explanation of Results

The main trick is to use volatile writes/reads and TLS (thread local storage),
to avoid using atomics for no-contention locks.  When only readers are present,
they are all effectively operating independently.

The asymmetric mutexes in this library can provide reader-lock frequency
in the 70MHz - 1GHz range, generally at the expense of writer frequency.
**This is one to two orders of magnitude faster than the fastest OS primitives.**

Because the threads operate independently, another important property of
these mutexes is that **you can throw more reader threads at a problem, and
get more overall throughput**.

## Design Constraints and Goals

The algorithms have varying starvation guarantees.
There are at least some "general purpose" algorithms where neither readers
nor writers can starve each other.
Higher performance can be gained by sacrificing starvation-proofness; typically
the decision is for writers to have precedence over readers, like for
VMs when a GC takes the write-lock.

Starvation guarantees are not the same as a
guarantee of **fairness**, which is discussed below.

There is also an observed trade-off between total throughput and fairness.
That is, the mutex designs with the highest throughput (combined
reader and writer locks per second) tend to be less fair.


### Fairness

What is fairness?  It is a guarantee that all clients will be serviced "equally".

It's easiest to think about fairness when all threads have the same OS scheduler
priority level.  In this case, all threads should be serviced in the order
in which they arrive at a mutex -- in a queue order -- to be perfectly fair.

The OS Mutex object has this property.  It is an exclusive access object only,
and threads get to acquire the mutex in the order in which they arrive.  OK, that's
a lie -- most schedulers are pseudorandom -- but *on average* the scheduler will
give each thread the same number of acquisitions per second.

When thread priority levels come into the picture, things get murkier.  But intuitively,
we know that a higher priority thread should get more mutex acquisitions per second
than a lower priority thread, if they're both continually trying to acquire and
release the mutex.

For this reason, when we design a new mutex type, we must keep
in mind that the OS scheduler is "the boss".  A usermode mutex should avoid
making thread-ordering decisions in usermode code; rather, it should delegate
those decisions to the kernel by having threads block on OS objects.

This makes the mutex-design-puzzle trickier, because we have two conflicting goals:

* Invoke blocking calls on kernel objects for fair scheduling.
* Avoid kernel calls as much as possible, because they're freakin' slow.

And to confound this even further, there are situations where an unfair design
will still provide greater throughput under contention, when compared to a fair design.


## Specific Designs, their Uses, and Perf

The benchmark charts in [Benchmarks.ods](Benchmarks.ods) speak volumes.  However,
those are not presented nicely, so I attempt to do so here.

The following descriptions are just a rough analysis, and may not be 100% accurate.

Benchmarks are all taken on a laptop with these specs:

- Intel i7 720QM (2800MHz -> 2400MHz -> 1800MHz -> 1600MHz -> 900MHz idle)
- 6GB of memory
- Win7 64-bit
- a bunch of stuff running in the background, but CPU is basically idle

### Win32 [Mutex](019_urwmutex/mutex.h)

* Capability: write-lock only; readers get exclusive access
* Starvation: never
* Fairness: perfect
* Reader Perf: low; 50kHz loaded, 2MHz straight
* Writer Perf: ""

### Win32 [CriticalSection](019_urwmutex/critical_section.h)

* Capability: write-lock only; readers get exclusive access
* Starvation: never
* Fairness: close to perfect
* Reader Perf: OK; 15MHz loaded, 45MHz straight
* Writer Perf: ""

### Win32 [SlimReadWriteLock](019_urwmutex/slim_rwlock.h)
* Capability: read and write lock
* Starvation: never
* Fairness: no, but not terrible
* Reader Perf: OK; 5-10MHz loaded, 50MHz straight
* Writer Perf: OK; 150kHz-5MHz loaded, 50MHz straight

### [SemaMutex](019_urwmutex/sema_mutex.h)

* Capability: write-lock only; readers get exclusive access
* Starvation: never
* Fairness: perfect
* Reader Perf: low; 50kHz loaded, 2MHz straight
* Writer Perf: ""
* Notes: This is exactly like a Win32 Mutex but implemented using a Win32 Semaphore.
  And what do we find out?  That the performance when directly using Semaphores sucks.

### [FairReadWriteMutex](019_urwmutex/fair_rwmutex.h)

* Capability: read and write lock
* Starvation: never
* Fairness: perfect
* Reader Perf: low; 66kHz loaded, 1MHz straight
* Writer Perf: low; 75kHz loaded, 1MHz straight
* Notes: Demonstrates a really elegant semaphore-based algorithm that enforces
  perfect fairness.  Used as a reference point when measuring fairness.

### [UltraSpinReadWriteMutex](019_urwmutex/ultraspin_rwmutex.h)

* Capability: read and write lock
* Starvation: writer starves reader
* Fairness: none
* Reader Perf: extremely volatile loaded (200kHz-150MHz), 200MHz-1GHz straight
* Writer Perf: 2.5kHz loaded, 2.5MHz straight
* Notes: Very simple algorithm that gives readers maximum throughput.  Use only if
  writers will almost never be present, or if the perf overhead of a writer doesn't
  matter at all.

### [UltraFastReadWriteMutex](019_urwmutex/ultrafast_rwmutex.h)

* Capability: read and write lock
* Starvation: writer starves reader
* Fairness: none
* Reader Perf: 10MHz-400MHz loaded, 200MHz-800MHz straight
* Writer Perf: 80kHz-100kHz loaded, 2.5MHz straight
* Notes: This is just a fully synchronized version of UltraSpinReadWriteMutex.

### [FastSlimReadWriteMutex](019_urwmutex/fastslim_rwmutex.h)

* Capability: read and write lock
* Starvation: none
* Fairness: no
* Reader Perf: 10MHz-400MHz loaded, 180MHz-450MHz straight
* Writer Perf: variable 200kHz-10MHz loaded, 40MHz straight
* Notes: This adds a TLS-layer to an ordinary SlimReadWriteLock, making pure reader
  perf much better.
  Generally its writer performance under load is worse than SRWL, but reader perf tends to be better.


### TODO: document remaining classes


## Additional Optimization Tricks

Here I document some of the surprises I hit, and how to solve them.

### Thread Local Storage

The Win32 TlsGetValue() function has an extraordinary amount of overhead, to the point
where it's worth writing your own inlinable version.  The OS function incurs several
penalties, including:

* additional call overhead (vs inlining)
* in 32-bit processes, additional ABI call overhead for param passing
  and stack-frame creation (setting up ebp)
* additional jmp overhead (if using an implib for kernel32)
* performs additional data writes to the TIB for the equivalent of SetLastError
* does extra error checking to see if 0 &lt;&eq; tlsIndex &lt; 0x1040

Here's the inline versions:

    #if defined(_M_IX86)
        inline void* InlineTlsGetValue(DWORD dwTlsIndex)
        {
            assert(dwTlsIndex < 0x1040);
        
            if (dwTlsIndex < 0x40)
            {
                return (void*)__readfsdword(0x0e10 + dwTlsIndex * 4);
            }
            else // TlsExpansionSlots
            {
                void** pp = (void**)__readfsdword(0x0F94);
                return pp ? pp[dwTlsIndex * 4 - 0x40 * 4] : NULL;
            }
        }
    #elif defined(_M_X64)
        inline void* InlineTlsGetValue(DWORD dwTlsIndex)
        {
            assert(dwTlsIndex < 0x1040);
        
            if (dwTlsIndex < 0x40)
            {
                return (void*)__readgsqword(dwTlsIndex * 8 + 0x1480);
            }
            else // TlsExpansionSlots
            {
                void** pp = (void**)__readgsqword(0x1780);
                return pp ? pp[dwTlsIndex * 8 - 0x40 * 8] : NULL;
            }
        }
    #else
        inline void* InlineTlsGetValue(DWORD dwTlsIndex)
        {
            return TlsGetValue(dwTlsIndex);
        }
    #endif


### SEH Frames (Structured-Exception-Handling)

Even if you set all the performance-related compiler options to maximum, and use /EHsc
for C++ exception handling only, MSVC still appears to generate some kind of
structured-exception-handling frame before calling any Win32 function.  In the
disassembly, the extra instructions look like this:

    01361180 6A FF                 push        0FFFFFFFFh 
    01361182 64 A1 00 00 00 00     mov         eax,dword ptr fs:[00000000h] 
    01361188 68 B8 14 37 01        push        offset __ehhandler$?initTlsData@UltraSpinReadWriteMutex@@AAEPAUTlsData@1@XZ (13714B8h) 
    0136118D 50                    push        eax  
    0136118E 64 89 25 00 00 00 00  mov         dword ptr fs:[0],esp 

    /* function body */

    01361236 8B 4C 24 20           mov         ecx,dword ptr [esp+20h] 
    // ...
    0136123F 64 89 0D 00 00 00 00  mov         dword ptr fs:[0],ecx 

The problem here is that, like most compilers, MSVC will generate this stuff as
part of your function prolog and epilog *if any part of your function invokes
a Win32 function*.

This is particularly bad when trying to write a minimal-overhead readLock() routine.

The solution was to factor out the code that called OS functions into separate
functions.  Now readLock() is inlined with the exact minimal instructions required,
and the separate function (with its SEH-frame overhead) is only called when
readLock() has figured out it needs to go down a slow (synchronizing) path.

I am not sure if the same problem exists in 64-bit compilations.

### ScopedReadLock class can cache the TLS Data pointer

Since ScopedReadLock gets to claim some stack storage between
the ReadLock() and ReadUnlock(), the pTlsData only needs to be
queried once from the OS.  This eliminates a small number of extra instructions.



### Each volatile word gets its own cacheline

Each write to a volatile word causes the issuing CPU core to gain ownership
of the word's cacheline.  So if many variables live in the same cacheline, ownership
can be thrashed between cores.  The solution is to add padding after each volatile variable
to ensure it resides in its own cacheline.

I used a line size of 64 to match the i7.

This is a standard trick, but worth mentioning.


## Potential Improvements

### Adding memory barrier annotations

The code in this project works on x86 and x86\_64 processors, because the memory model is so strong.

Other architectures would require explicit memory barriers between the various volatile loads and stores.

### TlsGetValue and TlsSetValue should be abstracted from ReadWriteMutex classes

Clearly it's not OK for every ReadWriteMutex object to allocate its own TLS slot, since TLS slots
are a scarce and valuable resource.  This is one of the several reasons the code here is just for
demonstration.

A production-worthy class would make TLS a policy object; then the client can implement
it however they want or need to.

One great opportunity for clients is when they *already needed their own TLS data*.  In such cases,
clients can place rwmutex-TLS-data into their own TLS structures, and avoid paying repeated TLS lookup costs.

This also implies that public variants of the lock/unlock functions that accept a TLS-policy object
need to exist.

As a sidenote, removal of TLS data when a thread is destroyed should also be handled correctly.  (the
sample code doesn't deal with that case at all)

### ReadWriteMutex classes should be templated on synchronization primitive types

Rather than hard-code a ReadWriteMutex to use CriticalSection, it ought to use a template type TMutex.

This also opens the opportunity to benchmark using different primitive types, and specialize
appropriately when porting across OSes.



## Reference Material

I came up with most of the algorithms in this library by myself;
notable exceptions are the following:

* QtReadWriteMutex : based on http://doc.trolltech.com/qq/qq11-mutex.html
* FairReadWriteMutex : based on http://vorlon.case.edu/~jrh23/338/HW3.pdf

However, I wanted to go back and see if such algorithms exist already.
In general, the fast algorithms in this library are related to the
"Dekker Lock", which turns out to be one of the first mutex algorithms!

[http://en.wikipedia.org/wiki/Dekker%27s_algorithm](http://en.wikipedia.org/wiki/Dekker%27s_algorithm)

Another keyword is "biased lock", where some threads are given privilege
(in the form of lower overhead locks).  Google found this good article:

[http://blogs.oracle.com/dave/entry/biased_locking_in_hotspot](http://blogs.oracle.com/dave/entry/biased_locking_in_hotspot)


## License = MIT License

Copyright (c) 2012 Avinash Baliga

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

