.PHONY: all _config build install uninstall clean

EXTRA=runtime/libmir.a

OCAMLFIND?=	ocamlfind

KFREEBSD_LIB=	$(shell ocamlfind printconf path)/mirage-kfreebsd

all: build

_config:
	./cmd configure kfreebsd

build: _config
	./cmd build
	ocamlbuild $(EXTRA)

install:
	./cmd install
	mkdir -p $(KFREEBSD_LIB)
	for l in $(EXTRA); do cp _build/$$l $(KFREEBSD_LIB); done

uninstall:
	./cmd uninstall

clean:
	./cmd clean
