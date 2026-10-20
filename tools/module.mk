# Included from modules/<name>/Makefile, including newly scaffolded modules.
PYTHON ?= python3
REGION ?= all
MODULE_NAME := $(notdir $(CURDIR))
.PHONY: all clean
all:
	$(PYTHON) ../../tools/build.py build --region $(REGION) --module "$(MODULE_NAME)"
clean:
	$(PYTHON) ../../tools/build.py clean --module "$(MODULE_NAME)"
