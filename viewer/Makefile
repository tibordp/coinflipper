SHELL := /bin/bash

OPAM_ENV := eval $$(opam env --switch=5.2.1) &&

.PHONY: build clean deps

build:
	$(OPAM_ENV) dune build

clean:
	$(OPAM_ENV) dune clean

deps:
	$(OPAM_ENV) opam install . --deps-only --yes
