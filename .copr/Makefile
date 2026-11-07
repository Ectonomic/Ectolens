# COPR's "make srpm" method: COPR clones the repository and runs, in a Fedora build root
# with network access,
#   make -f .copr/Makefile srpm outdir=DIR spec=SPEC
# This makes the source archive (the KPipeWire submodule included, as a stranger's clone
# with --recurse-submodules has it) and the source package from packaging/ectolens.spec,
# its release stamped with the commit's date and hash so every rebuild of it keeps them.
SHELL := /bin/bash
TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
outdir ?= $(CURDIR)
# COPR passes spec= as the package's Spec File setting, or the checkout folder when that is
# empty: a .spec file named there is used, anything else means ours
SPECFILE := $(if $(filter %.spec,$(spec)),$(spec),$(TOP)/packaging/ectolens.spec)

srpm:
	command -v git >/dev/null && command -v rpmbuild >/dev/null || dnf -y install git rpm-build
	git config --global --add safe.directory '*'
	cd "$(TOP)" && git submodule update --init --recursive
	set -e; work=$$(mktemp -d); cd "$(TOP)"; \
	  ver=$$(sed -n 's/^Version: *//p' "$(SPECFILE)"); name=ectolens-$$ver; \
	  commit=$$(git rev-parse --short=7 HEAD); date=$$(git log -1 --format=%cd --date=format:%Y%m%d); \
	  git archive --prefix=$$name/ -o "$$work/src.tar" HEAD; \
	  (cd 3rdparty/kpipewire && git archive --prefix=$$name/3rdparty/kpipewire/ -o "$$work/kpw.tar" HEAD); \
	  tar --concatenate -f "$$work/src.tar" "$$work/kpw.tar"; gzip -9 -c "$$work/src.tar" > "$$work/$$name.tar.gz"; \
	  sed "s/^%{!?snapinfo: %global snapinfo 0}$$/%global snapinfo $$date.git$$commit/" "$(SPECFILE)" > "$$work/ectolens.spec"; \
	  rpmbuild -bs "$$work/ectolens.spec" --define "_sourcedir $$work" --define "_srcrpmdir $(outdir)"
