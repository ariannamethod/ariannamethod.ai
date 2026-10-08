CC = cc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lm

# ═══ BLAS Acceleration (optional) ═══
# Use: make BLAS=1
# macOS: Apple Accelerate (AMX/Neural Engine, zero deps)
# Linux: OpenBLAS (install: apt install libopenblas-dev)
#
# Without BLAS=1: pure scalar C loops (portable, correct)
# With BLAS=1: cblas_sgemv for delta voice, cblas_sger for notorch
UNAME := $(shell uname)

ifdef BLAS
  ifeq ($(UNAME), Darwin)
    CFLAGS += -DUSE_BLAS -DACCELERATE
    LDFLAGS += -framework Accelerate
  else
    CFLAGS += -DUSE_BLAS
    LDFLAGS += -lopenblas
  endif
endif

.PHONY: all test test-amlc test-imports test-text test-text-lower test-text-input test-tokenizer test-records test-lists test-maps test-sampling test-numerical test-janus janus clean test-all test-blas amlc runner install notorch install-notorch

# ═══ Core AML ═══
all: libaml.a runner amlc

libaml.a: core/ariannamethod.o
	ar rcs $@ $^

core/ariannamethod.o: core/ariannamethod.c core/ariannamethod.h
	$(CC) $(CFLAGS) -c $< -o $@

# ═══ amlc — AML→C transpiler ═══
amlc: tools/amlc

tools/amlc: tools/amlc.c
	$(CC) $(CFLAGS) tools/amlc.c -o $@

# ═══ aml runner — CLI wrapper around libaml ═══
runner: runner/aml

runner/aml: runner/am.c libaml.a
	$(CC) $(CFLAGS) -Icore runner/am.c libaml.a -o $@ $(LDFLAGS)

# Canonical NoTorch sampling, numerical values, and tokenizers use an optional binding. The core archive
# stays usable on its own; build NoTorch's archive before invoking this target.
NOTORCH_ROOT ?= ../notorch
NOTORCH_INCLUDE ?= $(NOTORCH_ROOT)
NOTORCH_LIB ?= $(NOTORCH_ROOT)/libnotorch.a
NOTORCH_LDFLAGS ?=

notorch: all libaml_notorch.a runner/aml-notorch

libaml_notorch.a: core/aml_notorch.o
	ar rcs $@ $^

core/aml_notorch.o: core/aml_notorch.c core/ariannamethod.h $(NOTORCH_INCLUDE)/notorch.h $(NOTORCH_INCLUDE)/sentencepiece.h
	$(CC) $(CFLAGS) -Icore -I$(NOTORCH_INCLUDE) -c $< -o $@

runner/aml-notorch: runner/am.c libaml_notorch.a libaml.a $(NOTORCH_LIB)
	$(CC) $(CFLAGS) -DAML_WITH_NOTORCH -Icore runner/am.c libaml_notorch.a libaml.a $(NOTORCH_LIB) -o $@ $(LDFLAGS) $(NOTORCH_LDFLAGS) -lpthread

# ═══ CUDA backend (optional, requires nvcc + cuBLAS) ═══
# Build:   make cuda
# Install: make install-cuda PREFIX=/usr/local
#
# Produces system-wide libariannamethod_cuda.a + cuda.h header consumed
# by notorch (USE_AM_CUDA=1), metaharmonix, and any other organism that
# wants GPU primitives (GEMM, RMSNorm, SiLU, backward kernels) without
# duplicating its own CUDA backend.
.PHONY: cuda install-cuda

NVCC ?= nvcc
CUDA_PATH ?= /usr/local/cuda

cuda: libariannamethod_cuda.a

libariannamethod_cuda.a: core/ariannamethod_cuda.cu core/ariannamethod_cuda.h
	$(NVCC) -O2 -DUSE_CUDA -c core/ariannamethod_cuda.cu -o core/ariannamethod_cuda.o
	ar rcs $@ core/ariannamethod_cuda.o
	@echo "Built: libariannamethod_cuda.a (CUDA backend)"

# ═══ Install — system-wide baseline (system/Mac Neo style) ═══
PREFIX ?= /opt/homebrew
install: all
	install -d $(PREFIX)/bin $(PREFIX)/lib $(PREFIX)/include/ariannamethod
	install -m 0755 runner/aml $(PREFIX)/bin/aml
	install -m 0755 tools/amlc $(PREFIX)/bin/amlc
	install -m 0644 libaml.a $(PREFIX)/lib/libaml.a
	install -m 0644 core/ariannamethod.h $(PREFIX)/include/ariannamethod/ariannamethod.h

# NoTorch is installed independently. amlc discovers this bridge alongside
# libaml.a and libnotorch.a in the same prefix.
install-notorch: install notorch
	install -m 0755 runner/aml-notorch $(PREFIX)/bin/aml-notorch
	install -m 0644 libaml_notorch.a $(PREFIX)/lib/libaml_notorch.a

# Install CUDA library (optional — run `make cuda` first, then `make install-cuda`)
install-cuda: libariannamethod_cuda.a
	install -d $(PREFIX)/lib $(PREFIX)/include/ariannamethod
	install -m 0644 libariannamethod_cuda.a $(PREFIX)/lib/libariannamethod_cuda.a
	install -m 0644 core/ariannamethod_cuda.h $(PREFIX)/include/ariannamethod/cuda.h
	@echo "Installed: $(PREFIX)/lib/libariannamethod_cuda.a + $(PREFIX)/include/ariannamethod/cuda.h"

# ═══ AML Tests ═══
test: core/test_aml
	./core/test_aml

test-amlc: all
	bash tests/test_amlc.sh
	bash tests/test_amlc_runtime.sh
	bash tests/test_amlc_includes.sh

test-imports: all
	bash tests/test_aml_imports.sh

test-text: all
	bash tests/test_aml_text_runtime.sh

test-text-lower: all
	bash tests/test_aml_text_lower.sh

test-text-input: all
	bash tests/test_aml_text_input.sh

test-tokenizer: notorch
	NOTORCH_LIB="$(abspath $(NOTORCH_LIB))" NOTORCH_INCLUDE="$(abspath $(NOTORCH_INCLUDE))" NOTORCH_LDFLAGS="$(NOTORCH_LDFLAGS)" bash tests/test_aml_tokenizer.sh

test-records: all
	bash tests/test_aml_records.sh

test-lists: all
	bash tests/test_aml_lists.sh

test-maps: all
	bash tests/test_aml_maps.sh

test-sampling: notorch
	NOTORCH_LIB="$(abspath $(NOTORCH_LIB))" NOTORCH_INCLUDE="$(abspath $(NOTORCH_INCLUDE))" NOTORCH_LDFLAGS="$(NOTORCH_LDFLAGS)" bash tests/test_aml_sampling.sh

test-numerical: notorch
	NOTORCH_LIB="$(abspath $(NOTORCH_LIB))" NOTORCH_INCLUDE="$(abspath $(NOTORCH_INCLUDE))" NOTORCH_LDFLAGS="$(NOTORCH_LDFLAGS)" bash tests/test_aml_numerical.sh

core/test_aml: core/test_aml.c core/ariannamethod.c core/ariannamethod.h
	$(CC) $(CFLAGS) core/test_aml.c core/ariannamethod.c -o $@ $(LDFLAGS)

# ═══ BLAS Tests — compile and run with acceleration ═══
test-blas:
ifeq ($(UNAME), Darwin)
	$(CC) $(CFLAGS) -DUSE_BLAS -DACCELERATE core/test_aml.c core/ariannamethod.c -o core/test_aml_blas -lm -framework Accelerate
else
	$(CC) $(CFLAGS) -DUSE_BLAS core/test_aml.c core/ariannamethod.c -o core/test_aml_blas -lm -lopenblas
endif
	./core/test_aml_blas

# ═══ Janus — first transformer in AML ═══
# "Janus will grow like mycelium, without roots, without a trunk, without a flag."

janus: janus/libjanus.dylib

janus/libjanus.dylib: janus/janus.go janus/lang.go janus/go.mod
	cd janus && go build -buildmode=c-shared -o libjanus.dylib .

test-janus: janus/libjanus.dylib janus/test_janus_c.c
	$(CC) $(CFLAGS) janus/test_janus_c.c -Ljanus -ljanus -o janus/test_janus_c
	cd janus && DYLD_LIBRARY_PATH=. ./test_janus_c

# Full test: AML + Janus basic
test-all: test test-janus

# ═══ Clean ═══
clean:
	rm -f core/*.o core/test_aml core/test_aml_blas libaml.a libaml_notorch.a runner/aml runner/aml-notorch tools/amlc
	rm -f janus/libjanus.dylib janus/libjanus.h janus/test_janus_c
