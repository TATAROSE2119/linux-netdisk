CLANG?=clang
CC?=gcc
BPFTOOL?=bpftool
PKG_CONFIG?=pkg-config

ARCH ?= $(shell uname -m | sed 's/x86_64/x86/' \
			 | sed 's/arm.*/arm/' \
			 | sed 's/aarch64/arm64/' \
			 | sed 's/ppc64le/powerpc/' \
			 | sed 's/mips.*/mips/' \
			 | sed 's/riscv64/riscv/' \
			 | sed 's/loongarch64/loongarch/')


CFLAGS:= -g -O2 -Wall -Wextra	#用户态程序的编译选项
BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH)	#BPF的编译选项

LIBBPF_CFLAGS:=$(shell $(PKG_CONFIG) --cflags libbpf)# libbpf 头文件路径
LIBBPF_LIBS:=$(shell $(PKG_CONFIG) --libs libbpf)# libbpf 链接库

.PHONY: all clean

.DELETE_ON_ERROR:

all: netdisk_loader

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@.tmp 
	mv $@.tmp $@

netdisk_bpf.bpf.o: netdisk_bpf.bpf.c include/event.h vmlinux.h
	$(CLANG) $(BPF_CFLAGS) $(LIBBPF_CFLAGS)	\
	-I. -Iinclude	\
	-c $< -o $@

netdisk_bpf.skel.h: netdisk_bpf.bpf.o
	$(BPFTOOL) gen skeleton $< > $@.tmp
	mv $@.tmp $@

netdisk_loader: netdisk_loader.c netdisk_bpf.skel.h include/event.h
	$(CC) $(CFLAGS) $(LIBBPF_CFLAGS)	\
	-I. -Iinclude	\
	netdisk_loader.c -o $@	\
	$(LIBBPF_LIBS) -lelf -lz

clean:
	$(RM) netdisk_loader
	$(RM) netdisk_bpf.bpf.o
	$(RM) netdisk_bpf.skel.h
	$(RM) vmlinux.h
	$(RM) *.tmp

