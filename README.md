dcc6502
=======

Disassembler for 6502 processors.

# Features
* Simple command-line interface
* Single file, ANSI C source
* Annotation for IO addresses of Nintendo Entertainment System (NES) system registers
* Apple 2 / Atari style output via `-a`
* Cycle-counting output via `-c`
* Machine code display inline with the disassembly via `-d`
* Skip 'n' beginnign bytes of binary via `-b #`
* Assembly style output via `-s`


# Bug Fixes

* Instruction decoding changed from linear O(151) to O(1)
* Fixed off-by-one file length bug
  * Zero length files now work properly
* Fixed reading file one byte at a time to a single read
* Fixed buffer overflow memory access


# HOWTO Build/Compile

Compile via the de-facto make:

```
    make clean && make all
```

The included makefile has a bunch of options:

|Option |Effect|
|:------|:-----|
|clean  |Delete binary files                        |
|all    |Build code and binary test files           |
|help   |Show makefile help options                 |
|illegal|Build and test illegal 6502 opcodes        |
|isntall|Build and copy to /opt/local/bin/disasm6502|
|zero   |Build and test zero-length file            |

NOTE: The binary is installed into `/opt/local/bin/` as `disasm6502`
in order not to over-write any previous versions of `dcc6502`.


# History tidbit
The original 1.0 version of dcc6502 was written overnight on Christmas eve
1998. At the time, I (Tennessee Carmel-Veilleux) was a 16-year-old NES
hacker learning 6502 assembly. Of course, as many teenagers are, I was
a bit arrogant and really thought my code was pretty hot back then :)
Fast-forward 15 years and I'm a grown-up engineer who is quite a bit more
humble about his code. Looking back, I think the tool did the job, but
obviously, 15 years of experience later, I would have made it quite a
bit cleaner. The disassembler has floated online on miscalleanous NES
development sites since 1998. I decided to put it on github starting at
version 1.4 and I will be cleaning-up the code over until version 2.0. 

This disassembler has made the rounds and has been used for a lot of
different purposes by many different people over the years. Hopefully
it will continue to be useful going forward. 

