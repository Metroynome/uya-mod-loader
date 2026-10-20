PYTHON ?= python3
.DEFAULT_GOAL := all
REGION ?= all
LIBRARY ?= none
.PHONY: all release modules new-module check test clean clean-release module
all modules:
	$(PYTHON) tools/build.py build --region $(REGION)
release:
	$(MAKE) all REGION=all
	$(PYTHON) tools/package_release.py
	$(PYTHON) tools/check_release.py
module:
	$(PYTHON) tools/build.py build --region $(REGION) --module "$(NAME)"
clean-release:
	$(PYTHON) tools/clean_release.py
clean:
	$(PYTHON) tools/build.py clean
new-module:
	$(PYTHON) tools/build.py new "$(NAME)" --library "$(LIBRARY)"
check:
	$(PYTHON) tools/build.py check
test:
	mkdir -p build
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'
	$(CC) -std=gnu99 -fno-builtin -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -I. tests/runtime.c strings.c -o build/runtime-test
	./build/runtime-test
	$(CC) -std=gnu99 -fno-builtin -I. tests/modmenu.c -o build/modmenu-test
	./build/modmenu-test
	$(CC) -std=gnu99 -iquote . tests/config.c -o build/config-test
	./build/config-test
	$(CC) -std=gnu99 -fno-builtin -iquote . tests/mapscan.c -o build/mapscan-test
	./build/mapscan-test
	@set -e; for region in 1 2; do \
	  $(CC) -std=gnu99 -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -DUYA_MODULE_REGION=$$region -I. tests/maploader.c -o build/maploader-test-$$region; \
	  ./build/maploader-test-$$region; \
	  $(CC) -std=gnu99 -Wno-int-to-pointer-cast -DUYA_MODULE_REGION=$$region -I. tests/menu_backend.c -o build/menu-backend-test-$$region; \
	  ./build/menu-backend-test-$$region; \
	done
	@set -e; for region in 1 2 3; do \
	  $(CC) -std=gnu99 -Wno-int-to-pointer-cast -DUYA_MODULE_REGION=$$region -I. tests/platform.c -o build/platform-test-$$region; \
	  ./build/platform-test-$$region; \
	done
