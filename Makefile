# Public commands. Compiler/library rules live in Makefile.eeglobal.
SHELL := /bin/sh
.DEFAULT_GOAL := all
REGION ?= all
LIBRARY ?= none
REGIONS := $(if $(filter all,$(REGION)),ntscu pal ntscj,$(REGION))
export LC_ALL := C

.PHONY: all modules module catalog release check-release new-module clean clean-release test _region _assets
all modules module: catalog
	+@set -eu; for region in $(REGIONS); do $(MAKE) --no-print-directory _region REGION=$$region REQUESTED_REGION=$(REGION); done
	$(MAKE) --no-print-directory _assets

# Build metadata comes directly from each module's Makefile.defs.
catalog:
	@mkdir -p build
	+@set -eu; for defs in modules/*/Makefile.defs; do \
	  test -f "$$defs" || continue; \
	  $(MAKE) --no-print-directory -s -f Makefile.eeglobal MODULE=$${defs%/Makefile.defs} describe; \
	done > build/catalog.new
	@mv build/catalog.new build/catalog.txt

# Full builds pack modules consecutively. Individual builds retain their addresses
# and stage every selected image before publishing, so failures cannot mix layouts.
_region:
	+@set -eu; work=build/$(REGION); mkdir -p "$$work"; \
	awk -F'|' -v r='$(REGION)' 'index(" "$$5" "," "r" ")' build/catalog.txt > "$$work/catalog.new"; \
	pick='$(NAME)'; \
	if test -n "$$pick"; then \
	  if ! grep -q "^modules/$$pick|" "$$work/catalog.new"; then \
	    test '$(REQUESTED_REGION)' = all || { echo "$$pick does not support $(REGION)" >&2; exit 1; }; \
	    echo "$$pick: skipping unsupported $(REGION)"; exit 0; \
	  fi; \
	  if test ! -f "$$work/layout.txt"; then echo 'Establishing layout with a full build'; pick=; \
	  elif ! cmp -s "$$work/catalog.new" "$$work/catalog.txt"; then echo 'Module definitions changed; run make from the root' >&2; exit 1; fi; \
	fi; \
	rm -rf "$$work/publish"; mkdir -p "$$work/publish"; \
	if test -z "$$pick"; then $(MAKE) --no-print-directory -f Makefile.eeglobal REGION=$(REGION) image; fi; \
	address=$$((0xf2f80)); count=0; : > "$$work/layout.new"; \
	while IFS='|' read -r folder name lib mode supported objects; do \
	  test -z "$$pick" || test "$$folder" = "modules/$$pick" || continue; \
	  if test -n "$$pick"; then \
	    address=$$(awk -F'|' -v n="$$name" '$$1==n {print $$4}' "$$work/layout.txt"); \
	    test -n "$$address"; \
	  fi; \
	  $(MAKE) --no-print-directory -f Makefile.eeglobal REGION=$(REGION) MODULE="$$folder" IMAGE="$$name" ADDRESS="$$address" image; \
	  bytes=$$(cat "$$work/modules/$$name/size.txt"); \
	  printf '%s|%s|%s|%s|%s\n' "$$name" "$$lib" "$$mode" "$$address" "$$bytes" >> "$$work/layout.new"; \
	  address=$$(((address+bytes+63)&~63)); count=$$((count+1)); \
	done < "$$work/catalog.new"; \
	mkdir -p hostfs/uya/mods build/dist/hostfs/uya/mods; \
	if test -z "$$pick"; then \
	  mv "$$work/layout.new" "$$work/layout.txt"; mv "$$work/catalog.new" "$$work/catalog.txt"; \
	  rm -f hostfs/uya/mods/*.$(REGION).bin build/dist/hostfs/uya/mods/*.$(REGION).bin; \
	  cp "$$work/publish/loader.pnach" build/dist/mod-loader.$(REGION).pnach; \
	  echo "$(REGION): $$count modules, $$((address-0xf2f80))/$$((0x100000-0xf2f80)) arena bytes"; \
	else echo "$(REGION): rebuilt $$count modules at reserved addresses"; fi; \
	for bin in "$$work/publish/"*.bin; do \
	  test -f "$$bin" || continue; cp "$$bin" hostfs/uya/mods/; cp "$$bin" build/dist/hostfs/uya/mods/; \
	done

_assets: build/mapscan/mapscan.irx
	@mkdir -p hostfs/uya/mods/utils build/dist/hostfs/uya/mods/utils build/dist/hostfs/uya/mods/debug build/dist/hostfs/uya/config
	cp $< hostfs/uya/mods/utils/
	cp $< build/dist/hostfs/uya/mods/utils/
	@set -eu; dest=build/dist/hostfs/uya; \
	for list in modules.txt config/modules.txt mods/debug/exceptionhandler.bin; do \
	  if test -f "hostfs/uya/$$list"; then cp "hostfs/uya/$$list" "$$dest/$$list"; else rm -f "$$dest/$$list"; fi; \
	done; \
	cp hostfs/uya/maploader-online.txt "$$dest/"; \
	for asset in hostfs/uya/*.version hostfs/uya/*.wad hostfs/uya/*.world hostfs/uya/*.sound hostfs/uya/*.map hostfs/uya/*.bg hostfs/uya/*.thumb hostfs/uya/*.code; do \
	  test ! -f "$$asset" || cp "$$asset" "$$dest/"; \
	done

build/mapscan/mapscan.irx: $(wildcard iop/mapscan/*)
	mkdir -p build/mapscan
	cp -R iop/mapscan/. build/mapscan/
	$(MAKE) -C build/mapscan

release:
	$(MAKE) all REGION=all NAME=
	sh tools/release.sh
check-release:
	sh tools/release.sh check
clean-release:
	sh tools/release.sh clean
new-module:
	sh tools/new-module.sh "$(NAME)" "$(LIBRARY)"
clean:
	@case '$(NAME)' in *[!a-z0-9_-]*) echo 'Invalid module name' >&2; exit 1;; esac
ifeq ($(strip $(NAME)),)
	rm -rf build
	rm -f hostfs/uya/mods/*.ntscu.bin hostfs/uya/mods/*.pal.bin hostfs/uya/mods/*.ntscj.bin
else
	$(MAKE) --no-print-directory -f Makefile.eeglobal MODULE=modules/$(NAME) clean-module
endif

test:
	$(MAKE) -f tests/Makefile
