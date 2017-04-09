CC=gcc
CFLAGS=-O -Wall -Wextra

dcc6502: dcc6502.c
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f *.o dcc6502 dcc6502.exe illegal.bin zero.bin

# B = 42
# z = 7A
# { = 7B
# | = 7C
# ` = 60
# echo -n does NOT work, solution is to use /usr/bin/printf
# http://stackoverflow.com/questions/25836956/echo-command-output-n-in-a-file-from-makefile
illegal.bin:
	@printf %s "Bz{|\`" > illegal.bin

illegal: illegal.bin dcc6502
	 ./dcc6502 -d      illegal.bin

install: dcc6502
	echo "Copying to /opt/local/bin ... as: disasm6502"
	sudo cp dcc6502 /opt/local/bin/disasm6502

zero: dcc6502
	@rm        -f zero.bin
	touch         zero.bin
	 ./dcc6502 -d zero.bin

help:
	@echo "Makefile options:"
	@echo "================="
	@echo ""
	@echo "clean     Delete binary file"
	@echo "illegal   Test disassembly of bad opcodes"
	@echo "install   Install to /opt/local/bin"
	@echo "help      Show this makefile help options"
	@echo "zero      Test disassembly with zero-length file"

all: dcc6502 zero

