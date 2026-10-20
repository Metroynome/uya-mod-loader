PYTHON ?= python3
REGION ?= all
LIBRARY ?= none
.PHONY: all release modules new-module check test clean module
all release modules:
	$(PYTHON) tools/build.py build --region $(REGION)
module:
	$(PYTHON) tools/build.py build --region $(REGION) --module "$(NAME)"
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
	@set -e; for region in 1 2 3; do \
	  $(CC) -std=gnu99 -Wno-int-to-pointer-cast -DUYA_MODULE_REGION=$$region -I. tests/platform.c -o build/platform-test-$$region; \
	  ./build/platform-test-$$region; \
	done
