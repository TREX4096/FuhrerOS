# FuhrerOS top-level targets (INSTRUCTION §8). Host requirements: docker, qemu.
#   make                 build base image (compile + unit tests + rootfs)
#   make desktop         build desktop image (Xorg + Firefox)
#   make run             boot dev VM on this terminal
#   make test            full acceptance test in a fresh VM
#   make benchmark       static vs adaptive benchmark (FUHRER_VM=bench)
#   make unit            unit tests only (in the builder container)
#   make clean           remove VM disks and build outputs
.PHONY: all build desktop run debug test benchmark unit clean

all: build

build:
	./scripts/build.sh

desktop:
	FUHRER_PROFILE=desktop ./scripts/build.sh

run:
	./scripts/run.sh

debug:
	./scripts/debug.sh

test:
	./scripts/test.sh

benchmark:
	./scripts/benchmark.sh

unit:
	docker build --target build -f vm/image/Dockerfile -t fuhreros-unit .

clean:
	./scripts/reset.sh --stop || true
	rm -rf build out
	@echo "images in $${FUHRER_OUT:-~/.cache/fuhreros/out} kept; delete manually to rebuild from scratch"
